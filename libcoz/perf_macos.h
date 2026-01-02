/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * Copyright (c) 2025, macOS port additions
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#if !defined(CAUSAL_RUNTIME_PERF_MACOS_H)
#define CAUSAL_RUNTIME_PERF_MACOS_H

#ifdef __APPLE__

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/types.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ccutil/wrapped_array.h"

// Ring buffer size for samples (must be power of 2)
#define SAMPLE_BUFFER_SIZE 256
#define SAMPLE_BUFFER_MASK (SAMPLE_BUFFER_SIZE - 1)

// Sample structure stored in ring buffer
struct sample_entry {
  uint64_t ip;
  uint64_t time;
  uint64_t tid;
};

class perf_event {
public:
  enum class record_type;
  class record;

  /// Default constructor
  perf_event();

  /// Create a sampling event for the current thread (macOS version)
  perf_event(uint64_t sample_period_ns, pid_t pid = 0);

  /// Move constructor
  perf_event(perf_event&& other);

  /// Destructor
  ~perf_event();

  /// Move assignment
  void operator=(perf_event&& other);

  /// Get event count (number of samples collected)
  uint64_t get_count() const;

  /// Start sampling
  void start();

  /// Stop sampling
  void stop();

  /// Close the event
  void close();

  /// Configure signal delivery when samples are ready
  void set_ready_signal(int sig);

  /// Get the ready signal
  int get_ready_signal() const { return _ready_signal; }

  /// Sample data types (simplified for macOS)
  enum class sample : uint64_t {
    ip = (1U << 0),           // Instruction pointer
    pid_tid = (1U << 1),      // Process/thread ID
    time = (1U << 2),         // Timestamp
    callchain = (1U << 3),    // Call stack
    cpu = (1U << 4),          // CPU number
    _end = (1U << 5)
  };

  /// Check if sampling this data type
  inline bool is_sampling(sample s) const {
    return _sample_type & static_cast<uint64_t>(s);
  }

  /// Get read format (compatibility)
  inline uint64_t get_read_format() const {
    return 0;
  }

  /// Record types
  enum class record_type {
    sample = 1,
    lost = 2
  };

  /// A sample record
  struct record {
    friend class perf_event;
  public:
    record_type get_type() const { return _type; }

    inline bool is_sample() const { return get_type() == record_type::sample; }
    inline bool is_lost() const { return get_type() == record_type::lost; }

    uint64_t get_ip() const;
    uint64_t get_pid() const;
    uint64_t get_tid() const;
    uint64_t get_time() const;
    uint32_t get_cpu() const;
    ccutil::wrapped_array<uint64_t> get_callchain() const;

  private:
    record(record_type type) : _type(type) {}

    record_type _type;
    uint64_t _ip = 0;
    uint64_t _pid = 0;
    uint64_t _tid = 0;
    uint64_t _time = 0;
    uint32_t _cpu = 0;
  };

  class iterator {
  public:
    iterator(perf_event& source, bool at_end = false);

    void next();
    record get();
    bool has_data() const { return !_at_end; }

    iterator& operator++() { next(); return *this; }
    record operator*() { return get(); }
    bool operator!=(const iterator& other) { return has_data() != other.has_data(); }

  private:
    perf_event& _source;
    bool _at_end;
    size_t _read_index;
  };

  /// Get iterator to beginning
  iterator begin();

  /// Get iterator to end
  iterator end();

  // Ring buffer for samples
  sample_entry _samples[SAMPLE_BUFFER_SIZE];
  std::atomic<size_t> _write_index{0};
  size_t _read_index = 0;

  /// Whether sampling is active
  std::atomic<bool> _active{false};

  /// Sample count
  std::atomic<uint64_t> _sample_count{0};

  /// Mach thread port for the target thread
  mach_port_t _target_thread;

  /// pthread_t for the target thread (used to signal sample processing)
  pthread_t _target_pthread;

private:
  // Disallow copy and assignment
  perf_event(const perf_event&) = delete;
  void operator=(const perf_event&) = delete;

  /// Sample type configuration
  uint64_t _sample_type = 0;

  /// Sample period in nanoseconds
  uint64_t _sample_period_ns = 0;

  /// Signal to deliver when samples are ready
  int _ready_signal = 0;

  /// Process ID to sample (0 = current process)
  pid_t _pid = 0;
};

// Global sampling thread management
void macos_sampling_start();
void macos_sampling_stop();
void macos_sampling_register_event(perf_event* event);
void macos_sampling_unregister_event(perf_event* event);

#endif // __APPLE__
#endif // CAUSAL_RUNTIME_PERF_MACOS_H
