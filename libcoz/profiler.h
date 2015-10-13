#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "coz.h"

#include "inspect.h"
#include "progress_point.h"
#include "thread_state.h"
#include "util.h"

#include "ccutil/spinlock.h"
#include "ccutil/static_map.h"

/// Type of a thread entry function
typedef void* (*thread_fn_t)(void*);

/// The type of a main function
typedef int (*main_fn_t)(int, char**, char**);

enum {
  SampleSignal = SIGPROF, //< Signal to generate when samples are ready
  SamplePeriod = 1000000, //< Time between samples (1ms)
  SampleBatchSize = 10,   //< Samples to batch together for one processing run
  SpeedupDivisions = 20,  //< How many different speedups to try (20 = 5% increments)
  ZeroSpeedupWeight = 7,  //< Weight of speedup=0 versus other speedup values (7 = ~25% of experiments run with zero speedup)
  ExperimentMinTime = SamplePeriod * SampleBatchSize * 50,  //< Minimum experiment length
  ExperimentCoolOffTime = SamplePeriod * SampleBatchSize,   //< Time to wait after an experiment
  ExperimentTargetDelta = 5 //< Target minimum number of visits to a progress point during an experiment
};

class profiler {
public:
  /// Start the profiler
  void startup(const std::string& outfile,
               line* fixed_line,
               int fixed_speedup);

  /// Shut down the profiler
  void shutdown();

  /// Get or create a progress point to measure throughput
  throughput_point* get_throughput_point(const std::string& name);
  
  /// Get or create a progress point to measure latency
  latency_point* get_latency_point(const std::string& name);

  /// Pass local delay counts and excess delay time to the child thread
  int handle_pthread_create(pthread_t*, const pthread_attr_t*, thread_fn_t, void*);

  /// Force threads to catch up on delays, and stop sampling before the thread exits
  void handle_pthread_exit(void*) __attribute__((noreturn));

  /// Ensure a thread has executed all the required delays before possibly unblocking another thread
  void catch_up();

  /// Call before (possibly) blocking
  void pre_block();

  /// Call after unblocking. If by_thread is true, delays will be skipped
  void post_block(bool skip_delays);

  /// Only allow one instance of the profiler, and never run the destructor
  static profiler& get_instance() {
    static char buf[sizeof(profiler)];
    static profiler* p = new(buf) profiler();
    return *p;
  }

private:
  profiler()  {
    _experiment_active.store(false);
    _global_delay.store(0);
    _delay_size.store(0);
    _selected_line.store(nullptr);
    _next_line.store(nullptr);
    _running.store(true);
  }

  // Disallow copy and assignment
  profiler(const profiler&) = delete;
  void operator=(const profiler&) = delete;

  void profiler_thread(spinlock& l);          //< Body of the main profiler thread
  void begin_sampling(thread_state* state);   //< Start sampling in the current thread
  void end_sampling();                        //< Stop sampling in the current thread
  void add_delays(thread_state* state);       //< Add any required delays
  void process_samples(thread_state* state);  //< Process all available samples and insert delays
  line* find_line(perf_event::record&);       //< Map a sample to its source line
  void log_samples(std::ofstream&, size_t);   //< Log runtime and sample counts for all identified regions

  thread_state* add_thread(); //< Add a thread state entry for this thread
  thread_state* get_thread_state(); //< Get a reference to the thread state object for this thread
  void remove_thread(); //< Remove the thread state structure for the current thread

  static void* start_profiler_thread(void*);          //< Entry point for the profiler thread
  static void* start_thread(void* arg);               //< Entry point for wrapped threads
  static void samples_ready(int, siginfo_t*, void*);  //< Signal handler for sample processing
  static void on_error(int, siginfo_t*, void*);       //< Handle errors

  /// A map from name to throughput monitoring progress points
  std::unordered_map<std::string, throughput_point*> _throughput_points;
  spinlock _throughput_points_lock; //< Spinlock that protects the throughput points map
  
  /// A map from name to latency monitoring progress points
  std::unordered_map<std::string, latency_point*> _latency_points;
  spinlock _latency_points_lock;  //< Spinlock that protects the latency points map

  static_map<pid_t, thread_state> _thread_states;   //< Map from thread IDs to thread-local state

  std::atomic<bool> _experiment_active; //< Is an experiment running?
  std::atomic<size_t> _global_delay;    //< The global delay time required
  std::atomic<size_t> _delay_size;      //< The current delay size
  std::atomic<line*> _selected_line;    //< The line to speed up
  std::atomic<line*> _next_line;        //< The next line to speed up

  pthread_t _profiler_thread;     //< Handle for the profiler thread
  std::atomic<bool> _running;     //< Clear to signal the profiler thread to quit
  std::string _output_filename;   //< File for profiler output
  line* _fixed_line;              //< The only line that should be sped up, if set
  int _fixed_delay_size = -1;     //< The only delay size that should be used, if set

  /// Atomic flag to guarantee shutdown procedures run exactly one time
  std::atomic_flag _shutdown_run = ATOMIC_FLAG_INIT;
};

#endif
