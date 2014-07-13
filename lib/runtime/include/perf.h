#if !defined(CAUSAL_RUNTIME_PERF_H)
#define CAUSAL_RUNTIME_PERF_H

#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <sys/types.h>

#include <cstdint>
#include <functional>

#include "log.h"

class perf_event {
public:
  enum class record_type;
  class record;
  class sample_record;
  
  /// Default constructor
  perf_event();
  /// Open a perf_event file using the given options structure
  perf_event(struct perf_event_attr& pe, pid_t pid = 0, int cpu = -1);
  /// Move constructor
  perf_event(perf_event&& other);
  
  /// Close the perf event file and unmap the ring buffer
  ~perf_event();
  
  /// Move assignment is supported
  void operator=(perf_event&& other);
  
  /// Read event count
  uint64_t get_count() const;
  
  /// Start counting events and collecting samples
  void start();
  
  /// Stop counting events
  void stop();
  
  /// Get the file descriptor associated with this perf event
  int get_fd();
  
  /// Apply a function to all available records in the mmapped ring buffer
  void process(std::function<void(const record&)> handler);
  
  /// An enum to distinguish types of records in the mmapped ring buffer
  enum class record_type {
    mmap = PERF_RECORD_MMAP,
    lost = PERF_RECORD_LOST,
    comm = PERF_RECORD_COMM,
    exit = PERF_RECORD_EXIT,
    throttle = PERF_RECORD_THROTTLE,
    unthrottle = PERF_RECORD_UNTHROTTLE,
    fork = PERF_RECORD_FORK,
    read = PERF_RECORD_READ,
    sample = PERF_RECORD_SAMPLE,
    mmap2 = PERF_RECORD_MMAP2
  };
  
  /// Wrapper around a sample record from the mmapped ring buffer
  struct sample_record {
  private:
    struct sample_data {
      struct perf_event_header header;
      uint64_t ip;
      uint32_t pid, tid;
      uint64_t time;
      uint32_t cpu, res;
    } __attribute__((packed));
    
    sample_data* _data;
    
  public:
    inline explicit sample_record(struct perf_event_header* header) :
        _data(reinterpret_cast<sample_data*>(header)) {}
    
    inline uintptr_t get_ip() const { return _data->ip; }
    inline pid_t get_pid() const { return _data->pid; }
    inline pid_t get_tid() const { return _data->tid; }
    inline uint64_t get_time() const { return _data->time; }
    inline uint32_t get_cpu() const { return _data->cpu; }
  };
  
  /// A generic record type
  struct record {
  private:
    friend class perf_event;
    
    struct perf_event_header* _header;
    
    record(struct perf_event_header* header) : _header(header) {}
    
    record(const record&) = delete;
    record(record&&) = delete;
    void operator=(const record&) = delete;
    
  public:
    record_type get_type() const { return static_cast<record_type>(_header->type); }
    
    inline bool is_mmap() const { return get_type() == record_type::mmap; }
    inline bool is_lost() const { return get_type() == record_type::lost; }
    inline bool is_comm() const { return get_type() == record_type::comm; }
    inline bool is_exit() const { return get_type() == record_type::exit; }
    inline bool is_throttle() const { return get_type() == record_type::throttle; }
    inline bool is_unthrottle() const { return get_type() == record_type::unthrottle; }
    inline bool is_fork() const { return get_type() == record_type::fork; }
    inline bool is_read() const { return get_type() == record_type::read; }
    inline bool is_sample() const { return get_type() == record_type::sample; }
    inline bool is_mmap2() const { return get_type() == record_type::mmap2; }
    
    inline const sample_record as_sample() const {
      ASSERT(get_type() == record_type::sample) << "Casting perf_event record to wrong type!";
      return sample_record(_header);
    }
  };
    
private:
  // Disallow copy and assignment
  perf_event(const perf_event&) = delete;
  void operator=(const perf_event&) = delete;
  
  // Copy data out of the mmap ring buffer
  void copy_from_ring_buffer(size_t index, void* dest, size_t bytes);
  
  /// File descriptor for the perf event
  long _fd = -1;
  
  /// Memory mapped perf event region
  struct perf_event_mmap_page* _mapping = nullptr;
};

#endif
