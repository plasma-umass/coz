#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "causal.h"

#include "causal/counter.h"
#include "causal/inspect.h"
#include "causal/safe_map.h"
#include "causal/spinlock.h"
#include "causal/thread_state.h"
#include "causal/util.h"

/// Type of a thread entry function
typedef void* (*thread_fn_t)(void*);

/// The type of a main function
typedef int (*main_fn_t)(int, char**, char**);

enum {
  SampleSignal = SIGPROF, //< Signal to generate when samples are ready
  SamplePeriod = 10000000, //< Time between samples (10ms)
  SampleBatchSize = 10,   //< Samples to batch together for one processing run
  SpeedupDivisions = 20,  //< How many different speedups to try (20 = 5% increments)
  ExperimentMinTime = SamplePeriod * SampleBatchSize * 10,  //< Minimum experiment length
  ExperimentCoolOffTime = SamplePeriod * SampleBatchSize,   //< Time to wait after an experiment
  ExperimentMinCounterChange = 5  //< Minimum change in counters before experiment can end 
};

class profiler {
public:
  /// Start the profiler
  void startup(const std::string& outfile,
               line* fixed_line,
               int fixed_speedup,
               bool sample_only);
  
  /// Shut down the profiler
  void shutdown();
  
  /// Register a progress counter
  void register_counter(counter* c);
  
  /// Pass local delay counts and excess delay time to the child thread
  int handle_pthread_create(pthread_t*, const pthread_attr_t*, thread_fn_t, void*);
  
  /// Force threads to catch up on delays, and stop sampling before the thread exits
  void handle_pthread_exit(void*) __attribute__((noreturn));
  
  /// Ensure a thread has executed all the required delays before possibly unblocking another thread
  void catch_up();
  
  /// Call after unblocking. If by_thread is true, delays will be skipped
  void after_unblocking(bool by_thread);
  
  /// Only allow one instance of the profiler, and never run the destructor
  static profiler& get_instance() {
    static char buf[sizeof(profiler)];
    static profiler* p = new(buf) profiler();
    return *p;
  }

private:
  profiler()  {
    _experiment_active.store(false);
    _delays.store(0);
    _delay_size.store(0);
    _selected_line.store(nullptr);
    _next_line.store(nullptr);
    _samples.store(0);
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
  
  thread_state* add_thread(); //< Add a thread state entry for this thread
  thread_state* get_thread_state(); //< Get a reference to the thread state object for this thread
  void remove_thread(); //< Remove the thread state structure for the current thread
  
  static void* start_profiler_thread(void*);          //< Entry point for the profiler thread
  static void* start_thread(void* arg);               //< Entry point for wrapped threads
  static void samples_ready(int, siginfo_t*, void*);  //< Signal handler for sample processing
  static void on_error(int, siginfo_t*, void*);       //< Handle errors
  
  std::vector<counter*> _counters;  //< All the progress points
  spinlock _counters_lock;          //< Spinlock to protect the counters list
  
  safe_map<pid_t, thread_state> _thread_states;   //< Map from thread IDs to thread-local state
  
  std::atomic<bool> _experiment_active; //< Is an experiment running?
  std::atomic<size_t> _delays;          //< The total number of delays inserted
  std::atomic<size_t> _delay_size;      //< The current delay size
  std::atomic<line*> _selected_line;    //< The line to speed up
  std::atomic<line*> _next_line;        //< The next line to speed up
  
  std::string _output_filename;       //< File for profiler output
  line* _fixed_line;  //< The only line that should be sped up, if set
  int _fixed_delay_size = -1;         //< The only delay size that should be used, if set
  
  pthread_t _profiler_thread;         //< Handle for the profiler thread
  size_t _end_time;                   //< Time that shutdown was called
  bool _sample_only;                  //< Profiler should only collect samples, not insert delays
  std::atomic<size_t> _samples;       //< Total number of samples collected
  std::atomic<bool> _running;         //< Clear to signal the profiler thread to quit
  std::atomic_flag _shutdown_run = ATOMIC_FLAG_INIT;  //< Used to ensure shutdown only runs once
};

#endif
