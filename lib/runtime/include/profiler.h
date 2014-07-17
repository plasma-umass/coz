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
#include "support.h"

enum {
  SampleSignal = SIGPROF,
  SamplePeriod = 100000, // 100us
  SampleWakeupCount = 10,
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
  void thread_startup(size_t parent_round, size_t parent_delays);
  void thread_shutdown();
  
  inline size_t get_global_round() {
    return _global_round.load();
  }
  
  inline size_t get_global_delays() {
    return _global_delays.load();
  }
  
  size_t get_local_round();
  size_t get_local_delays();
  
  static profiler& get_instance() {
    static char buf[sizeof(profiler)];
    static profiler* p = new(buf) profiler();
    return *p;
  }

private:
  profiler() : _generator(get_time()), _delay_dist(0, SpeedupDivisions) {}
  profiler(const profiler&) = delete;
  void operator=(const profiler&) = delete;
  
  /// Process all available samples and insert delays. This operation will return false if the sampler is not immediately available.
  bool process_samples();
  
  /// Process all available samples and insert delays. This operation will block until it succeeds.
  void must_process_samples();
  
  /// Static wrapper for the sample processing function
  static void call_process_one_sample(const perf_event::record& r);
  
  /// Process a single sample (callback from perf_event)
  void process_one_sample(const perf_event::record& r);
  
  static void samples_ready(int signum, siginfo_t* info, void* p);
  
  /// Handle to the profiler's output
  output* _out;
  
  /// If specified, the fixed line that should be "sped up" for the whole execution
  std::shared_ptr<causal_support::line> _fixed_line;
  
  /// Flag is set when shutdown has been run
  std::atomic_flag _shutdown_run = ATOMIC_FLAG_INIT;
  
  /// The map of memory constructed by the causal_support library
  causal_support::memory_map _map;
  
  /// The current round number
  std::atomic<size_t> _global_round = ATOMIC_VAR_INIT(0);
  
  /// The total number of delays inserted this round
  std::atomic<size_t> _global_delays = ATOMIC_VAR_INIT(0);
  
  /// The number of samples collected this round
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
