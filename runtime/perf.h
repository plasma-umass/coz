#if !defined(CAUSAL_RUNTIME_PERF_H)
#define CAUSAL_RUNTIME_PERF_H

#include <asm/unistd.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <iostream>

#include "ringbuffer.h"

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static pid_t gettid() {
  return syscall(__NR_gettid);
}

class PerfSampler {
private:
  enum { DataPages = 8 };
  enum { PageSize = 0x1000 };
  enum { DataSize = DataPages * PageSize };
  enum { MmapSize = DataSize + PageSize };
  
  /// File descriptor for the opened perf_event file
  long _fd = -1;
  /// Pointer to the memory mapped perf_event file's header
  struct perf_event_mmap_page* _mmap = nullptr;
  /// Ring buffer of sample data from the perf_event file
  RingBuffer<DataSize> _data;
  /// Total number of samples collected
  size_t _samples = 0;
  
  PerfSampler(struct perf_event_attr& pe, int signum) {
    // Open the file
    _fd = perf_event_open(&pe, 0, -1, -1, 0);
    if(_fd == -1) {
      perror("Failed to open perf event");
      abort();
    }
    
    // If a signal was requested, set the file up for async access
    if(signal != 0) {
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
  
  struct PerfFileContents {
    uint64_t value;
    uint64_t time_enabled;
    uint64_t time_running;
  };
  
  PerfFileContents readFile() {
    PerfFileContents f;
    read(_fd, &f, sizeof(PerfFileContents));
    return f;
  }
  
public:
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
  
  PerfSampler() {}
  
  PerfSampler(PerfSampler&& s) {
    _fd = s._fd;
    s._fd = -1;
    _mmap = s._mmap;
    s._mmap = nullptr;
    _data = std::move(s._data);
    _samples = s._samples;
  }
  
  void operator=(PerfSampler&& s) {
    if(_mmap != nullptr) {
      munmap(_mmap, MmapSize);
    }
    
    if(_fd != -1) {
      close(_fd);
    }
    
    _fd = s._fd;
    s._fd = -1;
    _mmap = s._mmap;
    s._mmap = nullptr;
    _data = std::move(s._data);
    _samples = s._samples;
  }
  
  ~PerfSampler() {
    if(_mmap != nullptr) {
      munmap(_mmap, MmapSize);
    }
    
    if(_fd != -1) {
      close(_fd);
    }
  }
  
  static PerfSampler cycles(uint64_t period, int signum = 0) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.size = sizeof(struct perf_event_attr);
    pe.type = PERF_TYPE_HARDWARE;
    pe.config = PERF_COUNT_HW_CPU_CYCLES;
    pe.sample_period = period;
    pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TIME;
    pe.disabled = 1;
    pe.exclude_hv = 1;
    
    return PerfSampler(pe, signum);
  }
  
  static PerfSampler trips(void* address, uint64_t period, int signum = 0) {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.size = sizeof(struct perf_event_attr);
    pe.type = PERF_TYPE_BREAKPOINT;
    pe.bp_type = HW_BREAKPOINT_X;
    pe.bp_addr = (uint64_t)address;
    pe.bp_len = sizeof(long);
    pe.sample_period = period;
    pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TIME;
    pe.disabled = 1;
    pe.exclude_hv = 1;
    
    return PerfSampler(pe, signum);
  }
  
  void start(size_t max_events = 0) {
    if(max_events == 0) {
      ioctl(_fd, PERF_EVENT_IOC_ENABLE, 0);
    } else {
      ioctl(_fd, PERF_EVENT_IOC_REFRESH, max_events);
    }
  }
  
  void stop() {
    ioctl(_fd, PERF_EVENT_IOC_DISABLE, 0);
  }
  
  uint64_t count() {
    return readFile().value;
  }
  
  uint64_t timeEnabled() {
    return readFile().time_enabled;
  }
  
  uint64_t timeRunning() {
    return readFile().time_running;
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
  // Disallow copying
  PerfSampler(const PerfSampler&);
  
  // Disallow assignment
  void operator=(const PerfSampler*);
};

#endif
