#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "causal.h"
#include "counter.h"
#include "output.h"
#include "perf.h"
#include "support.h"

enum {
  SampleSignal = SIGPROF,
  SamplePeriod = 21000000, // 10ms
  SampleWakeupCount = 10,
  MinRoundSamples = 1000
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
  profiler() {}
  profiler(const profiler&) = delete;
  void operator=(const profiler&) = delete;
  
  /// Handle to the profiler's output
  output* _out;
  
  /// If specified, the fixed line that should be "sped up" for the whole execution
  std::shared_ptr<causal_support::line> _fixed_line;
  
  /// Flag is set when shutdown has been run
  std::atomic_flag _shutdown_run = ATOMIC_FLAG_INIT;
  
  /// The map of memory constructed by the causal_support library
  causal_support::memory_map _map;
  
  /// The current round number
  std::atomic<size_t> _global_round;
  
  /// The total number of delays inserted this round
  std::atomic<size_t> _global_delays;
  
  /// The number of samples collected this round
  std::atomic<size_t> _round_samples;
  
  /// The currently selected line for "speedup"
  std::shared_ptr<causal_support::line> _selected_line;
  
  /// The current delay size
  size_t _delay_size;
};

#endif
