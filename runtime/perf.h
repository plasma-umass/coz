#if !defined(CAUSAL_RUNTIME_PERF_H)
#define CAUSAL_RUNTIME_PERF_H

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
#include <unistd.h>

#include <atomic>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log.h"
#include "ringbuffer.h"
#include "spinlock.h"

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static pid_t gettid() {
  return syscall(__NR_gettid);
}

class PerfEvent {
public:
  /// Default constructor
  PerfEvent() {}
  
  /// Create a perf event from the settings structure
  PerfEvent(struct perf_event_attr& pe, pid_t pid = 0, int cpu = -1, int group_fd = -1, unsigned long flags = 0) {
    // Set some mandatory fields
    pe.size = sizeof(struct perf_event_attr);
    pe.disabled = 1;
    
    // Open the file
    _fd = perf_event_open(&pe, pid, cpu, group_fd, flags);
    REQUIRE(_fd != -1) << "Failed to open perf event";
    
    // If sampling, map the perf event file
    if(pe.sample_type != 0 && pe.sample_period != 0) {
      REQUIRE(pe.sample_type == (PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME)) << "Unsupported sample type";
      _mapping = MappedEvent(_fd);
    }
  }
  
  /// Move constructor
  PerfEvent(PerfEvent&& other) {
    _fd = __atomic_exchange_n(&other._fd, -1, __ATOMIC_SEQ_CST);
    if(_fd != -1) {
      _mapping = std::move(other._mapping);
    }
  }
  
  /// No copying allowed
  PerfEvent(const PerfEvent&) = delete;
  
  /// No assignment allowed
  void operator=(const PerfEvent&) = delete;
  
  /// Destructor
  ~PerfEvent() {
    // Atomically claim the file descriptor and close it if set
    int to_close = __atomic_exchange_n(&_fd, -1, __ATOMIC_SEQ_CST);
    if(to_close != -1) {
      close(to_close);
    }
  }
  
  /// Move assignment
  void operator=(PerfEvent&& other) {
    // take other perf event's file descriptor and replace it with -1
    _fd = __atomic_exchange_n(&other._fd, -1, __ATOMIC_SEQ_CST);
    if(_fd != -1) {
      _mapping = std::move(other._mapping);
    }
  }
  
  /// Read event count
  uint64_t getCount() const {
    uint64_t count;
    read(_fd, &count, sizeof(uint64_t));
    return count;
  }
  
  /// Start counting events
  void start() {
    REQUIRE(ioctl(_fd, PERF_EVENT_IOC_ENABLE, 0) != -1) << "Failed to start perf event";
  }
  
  /// Stop counting events
  void stop() {
    REQUIRE(ioctl(_fd, PERF_EVENT_IOC_DISABLE, 0) != -1) << "Failed to stop perf event";
  }
  
  int getFileDescriptor() {
    return _fd;
  }
  
  template<typename T>
  void process(T& t) {
    return _mapping.process<T>(t);
  }
  
  template<typename T>
  void process() {
    T t;
    return process(t);
  }
  
  struct SampleRecord {
  public:
    struct perf_event_header header;
    uint64_t ip;
    uint32_t pid, tid;
    uint64_t time;
    
    bool inUser() {
      return (header.misc & PERF_RECORD_MISC_CPUMODE_MASK) == PERF_RECORD_MISC_USER;
    }
  
    bool inKernel() {
      return (header.misc & PERF_RECORD_MISC_CPUMODE_MASK) == PERF_RECORD_MISC_KERNEL;
    }
  } __attribute__((packed));
  
  class MappedEvent {
  private:
    enum { DataPages = 16 };
    enum { PageSize = 0x1000 };
    enum { DataSize = DataPages * PageSize };
    enum { MmapSize = DataSize + PageSize };
    
  public:
    /// Default constructor
    MappedEvent() {}
    
    /// Map a file descriptor
    MappedEvent(int fd) {
      _header = (struct perf_event_mmap_page*)mmap(NULL, MmapSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      REQUIRE(_header != MAP_FAILED) << "Failed to mmap perf event file";
    }
    
    /// Move constructor
    MappedEvent(MappedEvent&& other) {
      _header = __atomic_exchange_n(&other._header, nullptr, __ATOMIC_SEQ_CST);
    }
    
    /// No copying allowed
    MappedEvent(const MappedEvent&) = delete;
    
    /// Destructor
    ~MappedEvent() {
      void* to_unmap = __atomic_exchange_n(&_header, nullptr, __ATOMIC_SEQ_CST);
      if(to_unmap != nullptr) {
        REQUIRE(munmap(to_unmap, MmapSize) != -1) << "Failed to munmap perf event file";
      }
    }
    
    /// No assignment allowed
    void operator=(const MappedEvent&) = delete;
    
    /// Move assignment
    void operator=(MappedEvent&& other) {
      _header = __atomic_exchange_n(&other._header, nullptr, __ATOMIC_SEQ_CST);
    }
    
    /**
     * Process any available records. Instantiate with a type that defines the following methods:
     *   void processMmap(const PerfEvent::MmapRecord&) (future)
     *   void processLost(const PerfEvent::LostRecord&) (future)
     *   void processComm(const PerfEvent::CommRecord&) (future)
     *   void processThrottle(const PerfEvent::ThrottleRecord&) (future)
     *   void processUnthrottle(const PerfEvent::UnthrottleRecord&) (future)
     *   void processFork(const PerfEvent::ForkRecord&) (future)
     *   void processRead(const PerfEvent::ReadRecord&) (future)
     *   void processSample(const PerfEvent::SampleRecord&)
     */
    template<typename T>
    void process(T& t) {
      if(_header == nullptr) {
        return;
      }
    
      // Read the start and end indices
      uint64_t data_tail = _header->data_tail;
      uint64_t data_head = _header->data_head;
      
      // Ensure ring buffer contents are up to date (required, according to manpage)
      __atomic_thread_fence(__ATOMIC_SEQ_CST);
    
      size_t index = data_tail;
      // Loop as long as there is space for at least one header
      while(index + sizeof(struct perf_event_header) < data_head) {
        struct perf_event_header hdr = copyData<struct perf_event_header>(index);
        
        // If the record hasn't been completely written, stop
        if(index + hdr.size > data_head) {
          break;
        }
        
        size_t rounded_index = index % DataSize;
        
        // Check if the record wraps around the ring buffer
        if(rounded_index + hdr.size > DataSize) {
          if(hdr.type == PERF_RECORD_SAMPLE) {
            if(hdr.size == sizeof(SampleRecord)) {
              const SampleRecord r = copyData<SampleRecord>(index);
              t.processSample(r);
            } else {
              WARNING << "Invalid sample record. Size = " << hdr.size;
            }
          } else {
            WARNING << "Unhandled record type " << hdr.type << ", size " << hdr.size;
            //WARNING << "Unhandled record type";
          }
          
        } else {
          void* record_base = (void*)((uintptr_t)_header + PageSize + rounded_index);
          if(hdr.type == PERF_RECORD_SAMPLE) {
            if(hdr.size == sizeof(SampleRecord)) {
              t.processSample(*reinterpret_cast<const SampleRecord*>(record_base));
            } else {
              WARNING << "Invalid sample record. Size = " << hdr.size;
            }
          } else {
             WARNING << "Unhandled record type " << hdr.type << ", size " << hdr.size;
            //WARNING << "Unhandled record type";
          }
        }
        
        index += hdr.size;
      }
   
      // Advance the tail pointer in the ring buffer
      _header->data_tail = index;
      __atomic_thread_fence(__ATOMIC_SEQ_CST);
    }
    
  private:
    /// Copy data out of the ring buffer from a given position
    template<typename T> T copyData(uint64_t pos) {
      uintptr_t base = (uintptr_t)_header + PageSize;
      uint64_t rounded_pos = pos % DataSize;
    
      // Check if the requested data will wrap around the ring buffer
      if(rounded_pos + sizeof(T) > DataSize) {
        // Data wraps. Copy in two chunks
        uint64_t first_chunk = DataSize - rounded_pos;
        char result_buffer[sizeof(T)];
        memcpy(result_buffer, (void*)(base + rounded_pos), first_chunk);
        memcpy((void*)((uintptr_t)result_buffer + first_chunk), (void*)base, sizeof(T) - first_chunk);
        return *reinterpret_cast<T*>(result_buffer);
      } else {
        // No wrapping. Just return the object
        return *reinterpret_cast<T*>(base + rounded_pos);
      }
    }
    
    /// The header page for the mapping
    struct perf_event_mmap_page* _header = nullptr;
  };
    
protected:
  /// File descriptor for the perf event
  long _fd = -1;
  
  /// Memory mapped perf event region
  MappedEvent _mapping;
};

class EventSet {
public:
  size_t add(PerfEvent&& e) {
    _l.lock();
    size_t index = _version++;
    e.start();
    _events.emplace(index, std::move(e));
    _l.unlock();
    return index;
  }
  
  size_t add(struct perf_event_attr& pe) {
    _l.lock();
    size_t index = _version++;
    _events.emplace(index, PerfEvent(pe));
    _events[index].start();
    _l.unlock();
    return index;
  }
  
  void remove(size_t index) {
    _l.lock();
    _to_remove.insert(index);
    _version++;
    _l.unlock();
  }
  
  void wait() {
    size_t newest_version = _version.load();
    
    // If we're at version zero, there aren't any events in the set
    if(newest_version == 0) {
      return;
    }
    
    // Check if the current version of the pollfd array is outdated
    if(_current_version != newest_version) {
      _l.lock();
      
      // Now that we have locked the set, ensure the version index is up to date
      newest_version = _version.load();
      
      // Remove perf events that have been passed to the remove() method since the last version
      for(size_t index : _to_remove) {
        auto iter = _events.find(index);
        if(iter != _events.end()) {
          _events.erase(iter);
        }
      }
      
      // Clear the removal list
      _to_remove.clear();
      
      // Resize the pollfds and event indices vectors
      _current_pollfds.resize(_events.size());
      _current_events.resize(_events.size());
      
      size_t i = 0;
      for(std::pair<const size_t, PerfEvent>& e : _events) {
        _current_pollfds[i] = {
          .fd = e.second.getFileDescriptor(),
          .events = POLLIN,
          .revents = 0
        };
        
        _current_events[i] = e.first;
        i++;
      }
      
      _current_version = newest_version;
      
      _l.unlock();
    }
    
    int rc = poll(_current_pollfds.data(), _current_pollfds.size(), -1);
    REQUIRE(rc != -1) << "Poll failed";
  }
  
  template<typename T>
  void process(T& t) {
    for(size_t i=0; i<_current_pollfds.size(); i++) {
      if(_current_pollfds[i].revents & POLLIN) {
        size_t index = _current_events[i];
        _events[index].process<T>(t);
        _current_pollfds[i].revents = 0;
      } else if(_current_pollfds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
        _current_pollfds[i].fd = -1;
      }
    }
  }
  
  template<typename T>
  void process() {
    T t;
    process(t);
  }
  
private:
  spinlock _l;
  std::unordered_map<size_t, PerfEvent> _events;
  std::unordered_set<size_t> _to_remove;
  std::atomic<size_t> _version = ATOMIC_VAR_INIT(0);
  
  size_t _current_version = 0;
  std::vector<struct pollfd> _current_pollfds;
  std::vector<size_t> _current_events;
};

#endif
