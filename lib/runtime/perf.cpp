#include "perf.h"

#include <asm/unistd.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
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

#include <functional>

#include "log.h"
#include "spinlock.h"

using std::function;

enum {
  DataPages = 16,
  PageSize = 0x1000,
  DataSize = DataPages * PageSize,
  MmapSize = DataSize + PageSize
};

long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static pid_t gettid() {
  return syscall(__NR_gettid);
}

// Create an uninitialized perf_event object
perf_event::perf_event() {}

// Open a perf_event file and map it (if sampling is enabled)
perf_event::perf_event(struct perf_event_attr& pe, pid_t pid, int cpu) {
  // Set some mandatory fields
  pe.size = sizeof(struct perf_event_attr);
  pe.disabled = 1;
  
  // Open the file
  _fd = perf_event_open(&pe, pid, cpu, -1, 0);
  REQUIRE(_fd != -1) << "Failed to open perf event";
  
  // If sampling, map the perf event file
  if(pe.sample_type != 0 && pe.sample_period != 0) {
    REQUIRE(pe.sample_type == (PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME 
                              | PERF_SAMPLE_CPU | PERF_SAMPLE_CALLCHAIN )) 
        << "Unsupported sample type";
    
    void* ring_buffer = mmap(NULL, MmapSize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    REQUIRE(ring_buffer != MAP_FAILED) << "Failed to mmap perf event file";
    
    _mapping = reinterpret_cast<struct perf_event_mmap_page*>(ring_buffer);
  }
}

/// Move constructor
perf_event::perf_event(perf_event&& other) {
  // Release resources if the current perf_event is initialized and not equal to this one
  if(_fd != -1 && _fd != other._fd) {
    close(_fd);
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
}

/// Close the perf_event file descriptor and unmap the ring buffer
perf_event::~perf_event() {
  if(_fd != -1)
    close(_fd);
  if(_mapping != nullptr)
    munmap(_mapping, MmapSize);
}

/// Move assignment
void perf_event::operator=(perf_event&& other) {
  // Release resources if the current perf_event is initialized and not equal to this one
  if(_fd != -1 && _fd != other._fd)
    close(_fd);
  if(_mapping != nullptr && _mapping != other._mapping)
    munmap(_mapping, MmapSize);
  
  // take other perf event's file descriptor and replace it with -1
  _fd = other._fd;
  other._fd = -1;
  
  // take other perf_event's mapping and replace it with nullptr
  _mapping = other._mapping;
  other._mapping = nullptr;
}

/// Read event count
uint64_t perf_event::get_count() const {
  uint64_t count;
  read(_fd, &count, sizeof(uint64_t));
  return count;
}

/// Start counting events
void perf_event::start() {
  REQUIRE(ioctl(_fd, PERF_EVENT_IOC_ENABLE, 0) != -1) << "Failed to start perf event: "
      << strerror(errno);
}

/// Stop counting events
void perf_event::stop() {
  REQUIRE(ioctl(_fd, PERF_EVENT_IOC_DISABLE, 0) != -1) << "Failed to stop perf event: "
      << strerror(errno);
}

int perf_event::get_fd() {
  return _fd;
}

void perf_event::process(function<void(const record&)> handler) {
  // If this isn't a sampling event, just return
  if(_mapping == nullptr)
    return;
  
  // Read the start and end indices
  uint64_t data_tail = _mapping->data_tail;
  uint64_t data_head = _mapping->data_head;
  
  // Ensure ring buffer contents are up to date (required, according to manpage)
  __atomic_thread_fence(__ATOMIC_SEQ_CST);

  size_t index = data_tail;
  // Loop as long as there is space for at least one header
  while(index + sizeof(struct perf_event_header) < data_head) {
    struct perf_event_header hdr;
    copy_from_ring_buffer(index, &hdr, sizeof(struct perf_event_header));
    
    // If the record hasn't been completely written, stop
    if(index + hdr.size > data_head) {
      break;
    }
    
    // Copy the record out of the ring buffer
    uint8_t record_data[hdr.size];
    copy_from_ring_buffer(index, record_data, hdr.size);
    
    struct perf_event_header* header = reinterpret_cast<struct perf_event_header*>(record_data);
    handler(perf_event::record(header));
    
    index += hdr.size;
  }

  // Advance the tail pointer in the ring buffer
  _mapping->data_tail = index;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void perf_event::copy_from_ring_buffer(uint64_t index, void* dest, size_t bytes) {
  uintptr_t base = reinterpret_cast<uintptr_t>(_mapping) + PageSize;
  size_t start_index = index % DataSize;
  size_t end_index = (index + bytes) % (DataSize + 1);
  
  if(start_index < end_index) {
    memcpy(dest, reinterpret_cast<void*>(base + start_index), bytes);
  } else {
    size_t chunk1_size = DataSize - start_index;
    size_t chunk2_size = bytes - chunk1_size;
    void* chunk2_dest = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(dest) + chunk1_size);
    memcpy(dest, reinterpret_cast<void*>(base + start_index), chunk1_size);
    memcpy(chunk2_dest, reinterpret_cast<void*>(base), chunk2_size);
  }
}
