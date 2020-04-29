/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include "perf.h"

#include <asm/unistd.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "util.h"

#include "ccutil/log.h"
#include "ccutil/spinlock.h"
#include "ccutil/wrapped_array.h"

using ccutil::wrapped_array;

enum {
  DataPages = 2,
  PageSize = 0x1000,
  DataSize = DataPages * PageSize,
  MmapSize = DataSize + PageSize
};

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

// Create an uninitialized perf_event object
perf_event::perf_event() {}

// Open a perf_event file and map it (if sampling is enabled)
perf_event::perf_event(struct perf_event_attr& pe, pid_t pid, int cpu) :
    _sample_type(pe.sample_type), _read_format(pe.read_format) {

  // Set some mandatory fields
  pe.size = sizeof(struct perf_event_attr);
  pe.disabled = 1;

  // Open the file
  _fd = perf_event_open(&pe, pid, cpu, -1, 0);
  if (_fd == -1) {
      std::string path = "/proc/sys/kernel/perf_event_paranoid";

      FILE *file = fopen(path.c_str(), "r");
      REQUIRE(file != NULL) << "Failed to open " << path << ": " << strerror(errno);

      char value_str[3];
      int res = fread(value_str, sizeof(value_str), 1, file);
      REQUIRE(res != -1) << "Failed to read from " << path << ": " << strerror(errno);

      value_str[2] = '\0';
      int value = atoi(value_str);

      FATAL << "Failed to open perf event. "
            << "Consider tweaking " << path << " to 2 or less "
            << "(current value is " << value << "), "
            << "or run coz as a privileged user (with CAP_SYS_ADMIN).";
  }

  // If sampling, map the perf event file
  if(pe.sample_type != 0 && pe.sample_period != 0) {
    void* ring_buffer = mmap(NULL, MmapSize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    REQUIRE(ring_buffer != MAP_FAILED) << "Mapping perf_event ring buffer failed. "
        << "Make sure the current user has permission to invoke the perf tool, and that "
        << "the program being profiled does not use an excessive number of threads (>1000).\n";

    _mapping = reinterpret_cast<struct perf_event_mmap_page*>(ring_buffer);
  }
}

/// Move constructor
perf_event::perf_event(perf_event&& other) {
  // Release resources if the current perf_event is initialized and not equal to this one
  if(_fd != -1 && _fd != other._fd) {
    ::close(_fd);
    INFO << "Closed perf event fd " << _fd;
  }

  if(_mapping != nullptr && _mapping != other._mapping)
    munmap(_mapping, MmapSize);

  // take other perf event's file descriptor and replace it with -1
  _fd = other._fd;
  other._fd = -1;

  // take other perf_event's mapping and replace it with nullptr
  _mapping = other._mapping;
  other._mapping = nullptr;

  // Copy over the sample type and read format
  _sample_type = other._sample_type;
  _read_format = other._read_format;
}

/// Close the perf_event file descriptor and unmap the ring buffer
perf_event::~perf_event() {
  close();
}

/// Move assignment
void perf_event::operator=(perf_event&& other) {
  // Release resources if the current perf_event is initialized and not equal to this one
  if(_fd != -1 && _fd != other._fd)
    ::close(_fd);
  if(_mapping != nullptr && _mapping != other._mapping)
    munmap(_mapping, MmapSize);

  // take other perf event's file descriptor and replace it with -1
  _fd = other._fd;
  other._fd = -1;

  // take other perf_event's mapping and replace it with nullptr
  _mapping = other._mapping;
  other._mapping = nullptr;

  // Copy over the sample type and read format
  _sample_type = other._sample_type;
  _read_format = other._read_format;
}

/// Read event count
uint64_t perf_event::get_count() const {
  uint64_t count;
  REQUIRE(read(_fd, &count, sizeof(uint64_t)) == sizeof(uint64_t))
    << "Failed to read event count from perf_event file";
  return count;
}

/// Start counting events
void perf_event::start() {
  if(_fd != -1) {
    REQUIRE(ioctl(_fd, PERF_EVENT_IOC_ENABLE, 0) != -1) << "Failed to start perf event: "
        << strerror(errno);
  }
}

/// Stop counting events
void perf_event::stop() {
  if(_fd != -1) {
    REQUIRE(ioctl(_fd, PERF_EVENT_IOC_DISABLE, 0) != -1) << "Failed to stop perf event: "
        << strerror(errno) << " (" << _fd << ")";
  }
}

void perf_event::close() {
  if(_fd != -1) {
    ::close(_fd);
    _fd = -1;
  }

  if(_mapping != nullptr) {
    munmap(_mapping, MmapSize);
    _mapping = nullptr;
  }
}

void perf_event::set_ready_signal(int sig) {
  // Set the perf_event file to async
  REQUIRE(fcntl(_fd, F_SETFL, fcntl(_fd, F_GETFL, 0) | O_ASYNC) != -1)
      << "failed to set perf_event file to async mode";

  // Set the notification signal for the perf file
  REQUIRE(fcntl(_fd, F_SETSIG, sig) != -1)
      << "failed to set perf_event file signal";

  // Set the current thread as the owner of the file (to target signal delivery)
  REQUIRE(fcntl(_fd, F_SETOWN, gettid()) != -1)
      << "failed to set the owner of the perf_event file";
}

void perf_event::iterator::next() {
  struct perf_event_header hdr;

  // Copy out the record header
  perf_event::copy_from_ring_buffer(_mapping, _index, &hdr, sizeof(struct perf_event_header));

  // Advance to the next record
  _index += hdr.size;
}

perf_event::record perf_event::iterator::get() {
  // Copy out the record header
  perf_event::copy_from_ring_buffer(_mapping, _index, _buf, sizeof(struct perf_event_header));

  // Get a pointer to the header
  struct perf_event_header* header = reinterpret_cast<struct perf_event_header*>(_buf);

  // Copy out the entire record
  perf_event::copy_from_ring_buffer(_mapping, _index, _buf, header->size);

  return perf_event::record(_source, header);
}

bool perf_event::iterator::has_data() const {
  // If there is no ring buffer, there is no data
  if(_mapping == nullptr) {
    return false;
  }

  // If there isn't enough data in the ring buffer to hold a header, there is no data
  if(_index + sizeof(struct perf_event_header) >= _head) {
    return false;
  }

  struct perf_event_header hdr;
  perf_event::copy_from_ring_buffer(_mapping, _index, &hdr, sizeof(struct perf_event_header));

  // If the first record is larger than the available data, nothing can be read
  if(_index + hdr.size > _head) {
    return false;
  }

  return true;
}

void perf_event::copy_from_ring_buffer(struct perf_event_mmap_page* mapping,
                                       ptrdiff_t index, void* dest, size_t bytes) {
  uintptr_t base = reinterpret_cast<uintptr_t>(mapping) + PageSize;
  size_t start_index = index % DataSize;
  size_t end_index = start_index + bytes;

  if(end_index <= DataSize) {
    memcpy(dest, reinterpret_cast<void*>(base + start_index), bytes);
  } else {
    size_t chunk2_size = end_index - DataSize;
    size_t chunk1_size = bytes - chunk2_size;

    void* chunk2_dest = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(dest) + chunk1_size);

    memcpy(dest, reinterpret_cast<void*>(base + start_index), chunk1_size);
    memcpy(chunk2_dest, reinterpret_cast<void*>(base), chunk2_size);
  }
}

uint64_t perf_event::record::get_ip() const {
  ASSERT(is_sample() && _source.is_sampling(sample::ip))
      << "Record does not have an ip field";
  return *locate_field<sample::ip, uint64_t*>();
}

uint64_t perf_event::record::get_pid() const {
  ASSERT(is_sample() && _source.is_sampling(sample::pid_tid))
      << "Record does not have a `pid` field";
  return locate_field<sample::pid_tid, uint32_t*>()[0];
}

uint64_t perf_event::record::get_tid() const {
  ASSERT(is_sample() && _source.is_sampling(sample::pid_tid))
      << "Record does not have a `tid` field";
  return locate_field<sample::pid_tid, uint32_t*>()[1];
}

uint64_t perf_event::record::get_time() const {
  ASSERT(is_sample() && _source.is_sampling(sample::time))
      << "Record does not have a 'time' field";
  return *locate_field<sample::time, uint64_t*>();
}

uint32_t perf_event::record::get_cpu() const {
  ASSERT(is_sample() && _source.is_sampling(sample::cpu))
      << "Record does not have a 'cpu' field";
  return *locate_field<sample::cpu, uint32_t*>();
}

wrapped_array<uint64_t> perf_event::record::get_callchain() const {
  ASSERT(is_sample() && _source.is_sampling(sample::callchain))
      << "Record does not have a callchain field";

  uint64_t* base = locate_field<sample::callchain, uint64_t*>();
  uint64_t size = *base;
  // Advance the callchain array pointer past the size
  base++;
  return wrapped_array<uint64_t>(base, size);
}

template<perf_event::sample s, typename T>
T perf_event::record::locate_field() const {
  uintptr_t p = reinterpret_cast<uintptr_t>(_header) + sizeof(struct perf_event_header);

  // Walk through the fields in the sample structure. Once the requested field is reached, return.
  // Skip past any unrequested fields that are included in the sample type

  /** ip **/
  if(s == sample::ip)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::ip))
    p += sizeof(uint64_t);

  /** pid, tid **/
  if(s == sample::pid_tid)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::pid_tid))
    p += sizeof(uint32_t) + sizeof(uint32_t);

  /** time **/
  if(s == sample::time)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::time))
    p += sizeof(uint64_t);

  /** addr **/
  if(s == sample::addr)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::addr))
    p += sizeof(uint64_t);

  /** id **/
  if(s == sample::id)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::id))
    p += sizeof(uint64_t);

  /** stream_id **/
  if(s == sample::stream_id)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::stream_id))
    p += sizeof(uint64_t);

  /** cpu **/
  if(s == sample::cpu)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::cpu))
    p += sizeof(uint32_t) + sizeof(uint32_t);

  /** period **/
  if(s == sample::period)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::period))
    p += sizeof(uint64_t);

  /** value **/
  if(s == sample::read)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::read)) {
    uint64_t read_format = _source.get_read_format();
    if(read_format & PERF_FORMAT_GROUP) {
      // Get the number of values in the read format structure
      uint64_t nr = *reinterpret_cast<uint64_t*>(p);
      // The default size of each entry is a u64
      size_t sz = sizeof(uint64_t);
      // If requested, the id will be included with each value
      if(read_format & PERF_FORMAT_ID)
        sz += sizeof(uint64_t);
      // Skip over the entry count, and each entry
      p += sizeof(uint64_t) + nr * sz;

    } else {
      // Skip over the value
      p += sizeof(uint64_t);
      // Skip over the id, if included
      if(read_format & PERF_FORMAT_ID)
        p += sizeof(uint64_t);
    }

    // Skip over the time_enabled field
    if(read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
      p += sizeof(uint64_t);
    // Skip over the time_running field
    if(read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
      p += sizeof(uint64_t);
  }

  /** callchain **/
  if(s == sample::callchain)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::callchain)) {
    uint64_t nr = *reinterpret_cast<uint64_t*>(p);
    p += sizeof(uint64_t) + nr * sizeof(uint64_t);
  }

  /** raw **/
  if(s == sample::raw)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::raw)) {
    uint32_t raw_size = *reinterpret_cast<uint32_t*>(p);
    p += sizeof(uint32_t) + raw_size;
  }

  /** branch_stack **/
  if(s == sample::branch_stack)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::branch_stack))
    FATAL << "Branch stack sampling is not supported";

  /** regs **/
  if(s == sample::regs)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::regs))
    FATAL << "Register sampling is not supported";

  /** stack **/
  if(s == sample::stack)
    return reinterpret_cast<T>(p);
  if(_source.is_sampling(sample::stack))
    FATAL << "Stack sampling is not supported";

  /** end **/
  if(s == sample::_end)
    return reinterpret_cast<T>(p);

  FATAL << "Unsupported sample field requested!";
}
