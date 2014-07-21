#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "causal.h"
#include "counter.h"
#include "output.h"
#include "perf.h"
#include "siglock.h"
#include "support.h"

/// Type of a thread entry function
typedef void* (*thread_fn_t)(void*);

enum {
  SampleSignal = SIGPROF,
  SamplePeriod = 1000000, // 1ms
  SampleWakeupCount = 50,
  MinRoundSamples = 200,
  SpeedupDivisions = 20
};

class profiler {
public:
  void include_file(const std::string& filename, uintptr_t load_address);
  void register_counter(Counter* c);
  void startup(const std::string& output_filename,
               const std::vector<std::string>& source_progress_names,
               const std::string& fixed_line_name);
  void shutdown();
  
  int handle_pthread_create(pthread_t*, const pthread_attr_t*, thread_fn_t, void*);
  void handle_pthread_exit(void*) __attribute__((noreturn));
  
  /// Take a snapshot of the global delay counter
  void snapshot_delays();
  
  /// Skip any global delays added since the last snapshot
  void skip_delays();
  
  /// Catch up on delays
  void catch_up();
  
  static profiler& get_instance() {
    static char buf[sizeof(profiler)];
    static profiler* p = new(buf) profiler();
    return *p;
  }

private:
  profiler() : _generator(get_time()), _delay_dist(0, SpeedupDivisions) {}
  profiler(const profiler&) = delete;
  void operator=(const profiler&) = delete;
  
  class thread_state {
  public:
    /// The number of samples processed since global delays were updated
    size_t processed = 0;
    /// The count of delays (or selected line visits) in the thread
    size_t delay_count = 0;
    /// Any excess delay time added when nanosleep() returns late
    size_t excess_delay = 0;
    /// A snapshot of the global delay count, taken before blocking on a pthread_* function
    size_t snapshot = 0;
    /// The sampler object for this thread
    perf_event sampler;
    
    class ref {
    public:
      ref(thread_state* s, siglock::context c, bool force = false) {
        if(s->_l.lock(c) || force) {
          _s = s;
        } else {
          _s = nullptr;
        }
      }
      
      ref(ref&& other) {
        _s = other._s;
        other._s = nullptr;
      }
      
      ~ref() {
        if(_s != nullptr) {
          _s->_l.unlock();
        }
      }
      
      inline void operator=(ref&& other) {
        _s = other._s;
        other._s = nullptr;
      }
      
      inline operator bool() {
        return _s != nullptr;
      }
      
      inline thread_state* operator->() {
        return _s;
      }
      
    private:
      ref(const ref&) = delete;
      void operator=(const ref&) = delete;
      
      thread_state* _s;
    };
    
  private:
    friend class ref;
    
    /// A siglock used to ensure access to thread state is atomic
    siglock _l;
  };
  
  /**
  * Get the thread local state in a reference. The `force` parameter should only be
  * true when in error-handling mode. This makes it possible to collect a backtrace, which
  * calls pthread_mutex_lock.
  */
  static thread_state::ref get_thread_state(siglock::context c, bool force = false) {
    static thread_local thread_state s;
    return thread_state::ref(&s, c, force);
  }
  
  /// Entry point for wrapped threads
  static void* start_thread(void* arg);
  
  /// Set up the sampler for the current thread
  void begin_sampling();
  
  /// Stop sampling in the current thread
  void end_sampling();
  
  /// Process all available samples and insert delays.
  void process_samples(thread_state::ref& state);
  
  /// Just insert delays
  void add_delays(thread_state::ref& state);
  
  /// Signal handler called when samples are ready to be processed
  static void samples_ready(int signum, siginfo_t* info, void* p);
  
  /// Handle an error condition signal
  static void on_error(int signum, siginfo_t* info, void* p);
  
  /// Handle to the profiler's output
  output* _out;
  
  /// If specified, the fixed line that should be "sped up" for the whole execution
  std::shared_ptr<causal_support::line> _fixed_line;
  
  /// Flag is set when shutdown has been run
  std::atomic_flag _shutdown_run = ATOMIC_FLAG_INIT;
  
  /// The map of memory constructed by the causal_support library
  causal_support::memory_map _map;
  
  /// The total number of delays inserted
  std::atomic<size_t> _global_delays = ATOMIC_VAR_INIT(0);
  
  /// The global delay count at the beginning of the current round
  std::atomic<size_t> _round_start_delays = ATOMIC_VAR_INIT(0);
  
  /// The number of samples collected
  std::atomic<size_t> _round_samples = ATOMIC_VAR_INIT(0);
  
  /**
   * The currently selected line for "speedup". This should never actually be read.
   * Only exists to ensure keep an accurate reference count. Use _selected_line instead.
   */
  std::shared_ptr<causal_support::line> _sentinel_selected_line;
  
  /**
   * The currently selected line for "speedup". Any thread that clears this line must
   * also clear the _sentinel_selected_line to decrement the reference count.
   */
  std::atomic<causal_support::line*> _selected_line = ATOMIC_VAR_INIT(nullptr);
  
  /// The current delay size
  std::atomic<size_t> _delay_size;
  
  /// Random number source
  std::default_random_engine _generator;
  
  /// Distribution for random delays
  std::uniform_int_distribution<size_t> _delay_dist;
};

#endif
