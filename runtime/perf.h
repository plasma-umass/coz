#if !defined(CAUSAL_RUNTIME_PERF_H)
#define CAUSAL_RUNTIME_PERF_H

#include <asm/unistd.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <iostream>

#include "log.h"
#include "ringbuffer.h"

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static pid_t gettid() {
  return syscall(__NR_gettid);
}

class PerfEvent {
public:
  PerfEvent(struct perf_event_attr& pe) {
    // Set some mandatory fields
    pe.size = sizeof(struct perf_event_attr);
    pe.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    pe.disabled = 1;
    
    // Open the file
    _fd = perf_event_open(&pe, 0, -1, -1, 0);
    if(_fd == -1) {
      perror("Failed to open perf event");
      abort();
    }
  }
  
  /// Move constructor
  PerfEvent(PerfEvent&& other) {
    _fd = other._fd;
    other._fd = -1;
  }
  
  /// Destructor
  ~PerfEvent() {
    if(_fd != -1) {
      close(_fd);
    }
  }
  
  /// Data read from the perf_event file
  struct State {
    uint64_t value;
    uint64_t time_enabled;
    uint64_t time_running;
  };
  
  /// Read event information
  State read() const {
    State c;
    ::read(_fd, &c, sizeof(State));
    return c;
  }
  
  /// Get the count of events
  uint64_t getCount() const {
    return read().value;
  }
  
  /// Start counting events
  void start() {
    ioctl(_fd, PERF_EVENT_IOC_ENABLE, 0);
  }
  
  /// Stop counting events
  void stop() {
    ioctl(_fd, PERF_EVENT_IOC_DISABLE, 0);
  }
    
protected:
  /// File descriptor for the perf event
  long _fd = -1;
  
private:
  /// Disallow copy and assignment
  PerfEvent(const PerfEvent&);
  void operator=(const PerfEvent&);
  void operator=(PerfEvent&&);
};

class PerfSampler : public PerfEvent {
private:
  enum { DataPages = 8 };
  enum { PageSize = 0x1000 };
  enum { DataSize = DataPages * PageSize };
  enum { MmapSize = DataSize + PageSize };
  
  /// Pointer to the memory mapped perf_event file's header
  struct perf_event_mmap_page* _mmap = nullptr;
  /// Ring buffer of sample data from the perf_event file
  RingBuffer<DataSize> _data;
  /// Total number of samples collected
  size_t _samples = 0;
  
public:
  PerfSampler(struct perf_event_attr& pe, int signum) : PerfEvent(pe) {
    pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TIME;
    
    // Set the perf_event file to async
    int ret = fcntl(_fd, F_SETFL, fcntl(_fd, F_GETFL, 0) | O_ASYNC);
    if(ret == -1) {
      perror("failed to set perf_event file to async mode");
      abort();
    }
    
    // Set the notification signal for the perf file
    ret = fcntl(_fd, F_SETSIG, signum);
    if(ret == -1) {
      perror("failed to set perf_event file signal");
      abort();
    }
    
    // Set the current thread as the owner of the file (to target signal delivery)
    ret = fcntl(_fd, F_SETOWN, gettid());
    if(ret == -1) {
      perror("failed to set the owner of the perf_event file");
      abort();
    }
    
    // memory map the file descriptor
    _mmap = (struct perf_event_mmap_page*)mmap(NULL, MmapSize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if(_mmap == MAP_FAILED) {
      perror("Failed to mmap perf event file descriptor");
      abort();
    }
    
    // set up the ring buffer
    _data = RingBuffer<DataSize>((uintptr_t)_mmap + PageSize);
  }
  
  PerfSampler(PerfSampler&& s) : PerfEvent(std::move(s)) {
    _mmap = s._mmap;
    s._mmap = nullptr;
    _data = std::move(s._data);
    _samples = s._samples;
  }
  
  ~PerfSampler() {
    void* p = _mmap;
    __atomic_store_n(&_mmap, NULL, __ATOMIC_SEQ_CST);
    munmap(p, MmapSize);
  }
  
  struct Sample {
  private:
    perf_event_header _hdr;
    uint64_t _ip;
    uint64_t _time;
    
  public:
    uint64_t getIP() {
      return _ip;
    }
    
    uint64_t getTime() {
      return _time;
    }
    
    bool inUser() {
      return (_hdr.misc & PERF_RECORD_MISC_CPUMODE_MASK) == PERF_RECORD_MISC_USER;
    }
    
    bool inKernel() {
      return (_hdr.misc & PERF_RECORD_MISC_CPUMODE_MASK) == PERF_RECORD_MISC_KERNEL;
    }
  };
  
  void start(size_t max_events = 0) {
    if(max_events == 0) {
      PerfEvent::start();
    } else {
      ioctl(_fd, PERF_EVENT_IOC_REFRESH, max_events);
    }
  }
  
  void processSamples(void (*callback)(Sample&)) {
    if(_mmap == nullptr) {
      return;
    }
    
    uint64_t data_tail = _mmap->data_tail;
    uint64_t data_head = _mmap->data_head;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    
    // Compute the number of bytes to read from the ring buffer
    size_t size = data_head - data_tail;
    
    size_t consumed_bytes = 0;
    while(consumed_bytes + sizeof(struct perf_event_header) < size) {
      // Take the next record's header from the ring buffer
      struct perf_event_header hdr = _data.peek<struct perf_event_header>();
      // If the whole record hasn't been written, stop
      if(consumed_bytes + hdr.size > size) {
        break;
      }
      
      // Check the type of record
      if(hdr.type == PERF_RECORD_SAMPLE) {
        // if this is a sample, take it and call the provided callback
        Sample s = _data.take<Sample>();
        _samples++;
        callback(s);
      } else {
        // Skip over the record
        _data.skip(sizeof(struct perf_event_header));
      }
      
      consumed_bytes += hdr.size;
    }
   
    // Advance the tail pointer in the ring buffer
    _mmap->data_tail += consumed_bytes;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
  }
  
private:
  // Disallow copy and assignment
  PerfSampler(const PerfSampler&);
  void operator=(const PerfSampler&);
  void operator==(PerfSampler&&);
};

#endif
