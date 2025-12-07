/*
 * macOS-specific sampling support that uses a user-space dispatch timer
 * to trigger stack captures.
 */

#if !defined(CAUSAL_RUNTIME_PERF_MACOS_H)
#define CAUSAL_RUNTIME_PERF_MACOS_H

#ifdef __APPLE__

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ccutil/log.h"
#include "ccutil/wrapped_array.h"

class perf_event {
public:
  enum class record_type {
    sample = 1,
    lost = 2
  };

  struct record {
    friend class perf_event;

    record_type get_type() const { return _type; }
    bool is_sample() const { return _type == record_type::sample; }
    bool is_lost() const { return _type == record_type::lost; }

    uint64_t get_ip() const;
    uint64_t get_pid() const;
    uint64_t get_tid() const;
    uint64_t get_time() const;
    uint32_t get_cpu() const;
    ccutil::wrapped_array<uint64_t> get_callchain() const;

  private:
    explicit record(record_type type) : _type(type) {}

    void reset(record_type type) {
      _type = type;
      _ip = 0;
      _pid = 0;
      _tid = 0;
      _time = 0;
      _cpu = 0;
      _callchain.clear();
    }

    void reserve_callchain(size_t count) {
      _callchain.reserve(count);
    }

    record_type _type;
    uint64_t _ip = 0;
    uint64_t _pid = 0;
    uint64_t _tid = 0;
    uint64_t _time = 0;
    uint32_t _cpu = 0;
    std::vector<uint64_t> _callchain;
  };

  class iterator {
  public:
    iterator(perf_event& source, bool at_end)
        : _source(source), _at_end(at_end) {}

    void next() { _at_end = true; }
    record get();
    bool has_data() const { return !_at_end && _source._has_sample; }

    iterator& operator++() { next(); return *this; }
    record operator*() { return get(); }
    bool operator!=(const iterator& other) { return has_data() != other.has_data(); }

  private:
    perf_event& _source;
    bool _at_end;
  };

  perf_event();
  explicit perf_event(uint64_t sample_period_ns, pid_t pid = 0);
  perf_event(perf_event&& other) noexcept;
  ~perf_event();

  void operator=(perf_event&& other) noexcept;

  uint64_t get_count() const;
  void start();
  void stop();
  void close();

  void set_ready_signal(int sig);
  void capture_sample(void* ucontext = nullptr);

  inline bool is_sampling(uint64_t) const { return true; }
  inline uint64_t get_read_format() const { return 0; }

  iterator begin();
  iterator end() { return iterator(*this, true); }

private:
  perf_event(const perf_event&) = delete;
  void operator=(const perf_event&) = delete;

  static void timer_callback(void* context);

  bool _active = false;
  uint64_t _sample_count = 0;
  uint64_t _sample_type = 0;
  uint64_t _sample_period_ns = 0;
  dispatch_source_t _timer = nullptr;
  dispatch_queue_t _queue = nullptr;
  int _ready_signal = 0;
  pid_t _pid = 0;
  pthread_t _signal_thread = nullptr;
  bool _has_sample = false;
  record _current_sample = record(record_type::sample);
};

#endif // __APPLE__
#endif // CAUSAL_RUNTIME_PERF_MACOS_H
