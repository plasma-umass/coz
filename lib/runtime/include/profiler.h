#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include "causal.h"
#include "counter.h"
#include "spinlock.h"
#include "support.h"
#include "thread_state.h"

/// Type of a thread entry function
typedef void* (*thread_fn_t)(void*);

enum {
  SampleSignal = SIGPROF,
  SamplePeriod = 1000000, // 1ms
  SampleWakeupCount = 10,
  SpeedupDivisions = 20,
  
  // Minimum number of samples before the experiment can end
  ExperimentMinSamples = 100,
  
  // Minimum change in every performance counter before the experiment can end
  ExperimentMinCounterChange = 5,
  
  // Minimum number of delays to insert before the experiment can end
  ExperimentMinDelays = 5,
  
  // The experiment will give up on the minimum delay count after this number of samples
  ExperimentAbortThreshold = 10000,
  
  // Reserve space for counters at startup
  InitCountersSize = 64
};

class profiler {
public:
  void register_counter(counter* c);
  void startup(const std::string& output_filename,
               std::shared_ptr<causal_support::line> fixed_line,
               int fixed_speedup);
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
  profiler() : _generator(get_time()),
               _delay_dist(0, SpeedupDivisions) {
    _counters.reserve(InitCountersSize);
  }
  
  profiler(const profiler&) = delete;
  void operator=(const profiler&) = delete;
  
  /// Entry point for wrapped threads
  static void* start_thread(void* arg);
  
  /// Set up the sampler for the current thread
  void begin_sampling();
  
  /// Stop sampling in the current thread
  void end_sampling();
  
  /// Process all available samples and insert delays.
  void process_samples(thread_state::ref& state);
  
  /// Find the source line that contains a given sample, walking the callchain if necessary
  std::shared_ptr<causal_support::line> find_containing_line(perf_event::record& sample);
  
  /// Just insert delays
  void add_delays(thread_state::ref& state);
  
  /// Is the program ready to start an experiment?
  bool experiment_ready();
  
  /// Is there currently an experiment running?
  bool experiment_running();
  
  /// Is the current experiment ready to end?
  bool experiment_finished();
  
  /// Start a new performance experiment
  void start_experiment(std::shared_ptr<causal_support::line> line, size_t delay_size);
  
  /// End the current performance experiment
  void end_experiment();
  
  /// Signal handler called when samples are ready to be processed
  static void samples_ready(int signum, siginfo_t* info, void* p);
  
  /// Handle an error condition signal
  static void on_error(int signum, siginfo_t* info, void* p);
  
  /// Profiler output file
  std::ofstream _output;
  
  /// Set of progress point counters
  std::vector<counter*> _counters;
  
  /// Set of values last seen in counters
  std::vector<size_t> _prev_counter_values;
  
  /// Lock to protect the profile log and counters lists
  spinlock _output_lock;
  
  /// If specified, the fixed line that should be "sped up" for the whole execution
  std::shared_ptr<causal_support::line> _fixed_line;
  
  /// If specified, selected lines will always run with the following delay size
  size_t _fixed_delay_size = -1;
  
  /// Flag is set when shutdown has been run
  std::atomic_flag _shutdown_run = ATOMIC_FLAG_INIT;
  
  /// The total number of delays inserted
  std::atomic<size_t> _global_delays = ATOMIC_VAR_INIT(0);
  
  /// The global delay count at the beginning of the current round
  std::atomic<size_t> _round_start_delays = ATOMIC_VAR_INIT(0);
  
  /// The number of samples collected
  std::atomic<size_t> _experiment_samples = ATOMIC_VAR_INIT(0);
  
  /// The currently selected line for "speedup"
  std::atomic<causal_support::line*> _selected_line = ATOMIC_VAR_INIT(nullptr);
  
  /// The current delay size
  std::atomic<size_t> _delay_size;
  
  /// Random number source
  std::default_random_engine _generator;
  
  /// Distribution for random delays
  std::uniform_int_distribution<size_t> _delay_dist;
};

#endif
