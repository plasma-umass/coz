#include "profiler.h"

#include <asm/unistd.h>
#include <execinfo.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "counter.h"
#include "log.h"
#include "perf.h"
#include "spinlock.h"
#include "support.h"
#include "timer.h"
#include "util.h"

using namespace causal_support;
using namespace std;

/**
 * Start the profiler
 */
void profiler::startup(const string& outfile, line* fixed_line, int fixed_speedup) {
  // Set up the sampling signal handler
  struct sigaction sa = {
    .sa_sigaction = profiler::samples_ready,
    .sa_flags = SA_SIGINFO | SA_ONSTACK
  };
  real::sigaction(SampleSignal, &sa, nullptr);
  
  // Set up handlers for errors
  sa = {
    .sa_sigaction = on_error,
    .sa_flags = SA_SIGINFO
  };
  real::sigaction(SIGSEGV, &sa, nullptr);
  real::sigaction(SIGABRT, &sa, nullptr);
  
  // Save the output file name
  _output_filename = outfile;
  
  // If a non-empty fixed line was provided, set it
  if(fixed_line)
    _fixed_line = fixed_line;
  
  // If the speedup amount is in bounds, set a fixed delay size
  if(fixed_speedup >= 0 && fixed_speedup <= 100)
    _fixed_delay_size = SamplePeriod * fixed_speedup / 100;

  // Use a spinlock to wait for the profiler thread to finish intialization
  spinlock l;
  l.lock();

  // Create the profiler thread
  INFO << "Starting profiler thread";
  int rc = real::pthread_create(&_profiler_thread, nullptr, profiler::start_profiler_thread, (void*)&l);
  REQUIRE(rc == 0) << "Failed to start profiler thread";
  
  // Double-lock l. This blocks until the profiler thread unlocks l
  l.lock();
  
  // Begin sampling in the main thread
  begin_sampling();
}

/**
 * Body of the main profiler thread
 */
void profiler::profiler_thread(spinlock& l) {
  // Open the output file
  ofstream output;
  output.open(_output_filename, ios_base::app);
  
  // Initialize the delay size RNG
  default_random_engine generator(get_time());
  uniform_int_distribution<size_t> delay_dist(0, SpeedupDivisions);
  
  // Log the start of this execution
  output << "startup\t"
         << "time=" << get_time() << "\t"
         << "sample-period=" << SamplePeriod << "\n";
  
  // Unblock the main thread
  l.unlock();
  
  vector<size_t> saved_counters;
  
  while(_running) {
    line* selected;
    // If this run has a fixed line, use it
    if(_fixed_line) {
      selected = _fixed_line;
    } else {
      // Otherwise, wait for the next line to be selected
      selected = _next_line.load(memory_order_relaxed);
      while(_running && selected == nullptr) {
        wait(SamplePeriod * SampleBatchSize);
        selected = _next_line.load(memory_order_relaxed);
      }
      if(!_running) break;
      _selected_line.store(selected, memory_order_relaxed);
    }
    
    size_t delay_size;
    // Choose a delay size
    if(_fixed_delay_size >= 0)
      delay_size = _fixed_delay_size;
    else
      delay_size = delay_dist(generator) * SamplePeriod / SpeedupDivisions;
    
    _delay_size.store(delay_size, memory_order_relaxed);
    
    size_t start_time = get_time();
    size_t starting_delays = _delays.load(memory_order_relaxed);
    
    // Log the start of the experiment
    output << "start-experiment\t"
           << "line=" << selected << "\t"
           << "time=" << start_time << "\t"
           << "selected-line-samples=" << selected->get_samples() << "\t"
           << "global-delays=" << starting_delays << "\n";
  
    // Save and log all counters
    saved_counters.clear();
    _counters_lock.lock();
    for(counter* c : _counters) {
      saved_counters.push_back(c->get_count());
      output << c;
    }
    _counters_lock.unlock();
    
    // Tell threads to start the experiment
    _experiment_active.store(true, memory_order_relaxed);
    
    bool experiment_finished = false;
    size_t wait_time = ExperimentMinTime;
    
    do {
      // Wait for the experiment to run
      wait(wait_time);
      
      // Have enough delays been inserted?
      if(_delays.load(memory_order_relaxed) - starting_delays >= ExperimentMinDelays
          || get_time() - start_time > ExperimentAbortThreshold) {
        experiment_finished = true;
        
        // Check if counters have changed enough to finish
        _counters_lock.lock();
        for(size_t i=0; i<saved_counters.size(); i++) {
          if(_counters[i]->get_count() - saved_counters[i] < ExperimentMinCounterChange)
            experiment_finished = false;
        }
        _counters_lock.unlock();
      }
      // Could increase wait time here, but that might delay exiting
    } while(_running && !experiment_finished);
    
    // Log the end of the experiment, whether it finished or the profiler is shutting down
    output << "end-experiment\t"
           << "time=" << get_time() << "\t"
           << "delay-size=" << delay_size << "\t"
           << "global-delays=" << _delays.load(memory_order_relaxed) << "\t"
           << "selected-line-samples=" << selected->get_samples() << "\n";
    
    // Log counter values
    _counters_lock.lock();
    for(counter* c : _counters) {
      output << c;
    }
    _counters_lock.unlock();
    
    // Clear the next line, so threads will select one
    _next_line.store(nullptr, memory_order_relaxed);
    
    // End the experiment
    _experiment_active.store(false, memory_order_relaxed);
    
    if(_running)
      wait(ExperimentCoolOffTime);
  }
  
  output << "shutdown\t"
         << "time=" << _end_time << "\t"
         << "samples=" << _samples << "\n";
  
  // Log sample counts for all observed lines
  for(const auto& file_entry : memory_map::get_instance().files()) {
    for(const auto& line_entry : file_entry.second->lines()) {
      shared_ptr<line> l = line_entry.second;
      if(l->get_samples() > 0) {
        output << "samples\t"
               << "line=" << l << "\t"
               << "count=" << l->get_samples() << "\n";
      }
    }
  }
  
  output.flush();
  output.close();
}

/**
 * Terminate the profiler thread, then exit
 */
void profiler::shutdown() {
  if(_shutdown_run.test_and_set() == false) {
    // Stop sampling in the main thread
    end_sampling();
    
    // Save the true end time and signal the profiler thread to stop
    _end_time = get_time();
    _running.store(false, memory_order_relaxed);
    
    // Join with the profiler thread
    real::pthread_join(_profiler_thread, nullptr);
  }
}

/**
 * Register a new progress counter
 */
void profiler::register_counter(counter* c) {
  _counters_lock.lock();
  _counters.push_back(c);
  _counters_lock.unlock();
};

/**
 * Argument type passed to wrapped threads
 */
struct thread_start_arg {
  thread_fn_t _fn;
  void* _arg;
  size_t _parent_delay_count;
  size_t _parent_excess_delay;
  
  thread_start_arg(thread_fn_t fn, void* arg, size_t c, size_t t) :
      _fn(fn), _arg(arg), _parent_delay_count(c), _parent_excess_delay(t) {}
};

/**
 * Entry point for wrapped threads
 */
void* profiler::start_thread(void* p) {
  thread_start_arg* arg = reinterpret_cast<thread_start_arg*>(p);
  
  // Set up thread state. Be sure to release the state lock before running the real thread function
  {
    auto state = thread_state::get(siglock::thread_context);
    REQUIRE(state) << "Failed to acquire exclusive access to thread state on thread startup";
  
    // Copy over the delay count and excess delay time from the parent thread
    state->delay_count = arg->_parent_delay_count;
    state->excess_delay = arg->_parent_excess_delay;
  }
  
  // Make local copies of the function and argument before freeing the arg wrapper
  thread_fn_t real_fn = arg->_fn;
  void* real_arg = arg->_arg;
  delete arg;
  
  // Start the sampler for this thread
  profiler::get_instance().begin_sampling();
  
  // Run the real thread function
  void* result = real_fn(real_arg);
  
  // Always exit via pthread_exit
  pthread_exit(result);
}

/**
 * Wrap calls to pthread_create so children inherit delay counts
 */
int profiler::handle_pthread_create(pthread_t* thread,
                                    const pthread_attr_t* attr,
                                    thread_fn_t fn,
                                    void* arg) {
  
  thread_start_arg* new_arg;
  
  // Get exclusive access to the thread-local state and set up the wrapped thread argument
  {
    auto state = thread_state::get(siglock::thread_context);
    REQUIRE(state) << "Unable to acquire exclusive access to thread state in pthread_create";
  
    // Allocate a struct to pass as an argument to the new thread
    new_arg = new thread_start_arg(fn, arg,
                                   state->delay_count,
                                   state->excess_delay);
  }
  
  // Create a wrapped thread and pass in the wrapped argument
  return real::pthread_create(thread, attr, profiler::start_thread, new_arg);
}

/**
 * Stop sampling and catch up on delays before a thread can exit
 */
void profiler::handle_pthread_exit(void* result) {
  end_sampling();
  catch_up();
  real::pthread_exit(result);
}

/**
 * Skip any missing delays. The thread that wakes this one should have run them already
 */
void profiler::skip_delays() {
  auto state = thread_state::get(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in skip_delays()";
  
  // Skip all missing delays
  state->delay_count = _delays.load(memory_order_relaxed);
  state->excess_delay = 0;
}

/**
 * Process all available samples and insert delays
 */
void profiler::catch_up() {
  auto state = thread_state::get(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in catch_up()";
  
  // Catch up on delays before unblocking any threads
  process_samples(state);
}

void profiler::begin_sampling() {
  auto state = thread_state::get(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in begin_sampling()";
  
  // Set the perf_event sampler configuration
  struct perf_event_attr pe = {
    .type = PERF_TYPE_SOFTWARE,
    .config = PERF_COUNT_SW_TASK_CLOCK,
    .sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_CALLCHAIN,
    .sample_period = SamplePeriod,
    .wakeup_events = SampleBatchSize, // This is ignored on linux 3.13 (why?)
    .exclude_idle = 1,
    .exclude_kernel = 1,
    .disabled = 1
  };
  
  // Create this thread's perf_event sampler and start sampling
  state->sampler = perf_event(pe);
  state->process_timer = timer(SampleSignal);
  state->process_timer.start_interval(SamplePeriod * SampleBatchSize);
  //state->sampler.set_ready_signal(SampleSignal);
  state->sampler.start();
}

void profiler::end_sampling() {
  auto state = thread_state::get(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in end_sampling()";
  
  process_samples(state);
  
  state->sampler.stop();
  state->sampler.close();
}

line* profiler::find_line(perf_event::record& sample) {
  if(!sample.is_sample())
    return nullptr;
  
  // Check if the sample occurred in known code
  line* l = memory_map::get_instance().find_line(sample.get_ip()).get();
  if(l)
    return l;
  
  // Walk the callchain
  for(uint64_t pc : sample.get_callchain()) {
    // Need to subtract one. PC is the return address, but we're looking for the callsite.
    l = memory_map::get_instance().find_line(pc-1).get();
    if(l)
      return l;
  }
  
  // No hits. Return null
  return nullptr;
}

void profiler::add_delays(thread_state::ref& state) {
  // Add delays if there is an experiment running
  if(_experiment_active) {
    // Take a snapshot of the global and local delays
    size_t delays = _delays;
    size_t delay_size = _delay_size;
    
    // Is this thread ahead or behind on delays?
    if(state->delay_count > delays) {
      // Thread is ahead: increase the global delay count
      _delays.fetch_add(state->delay_count - delays, memory_order_relaxed);
      
    } else if(state->delay_count < delays) {
      // Behind: Pause this thread to catch up
      size_t time_to_wait = (delays - state->delay_count) * delay_size;
      
      // Has this thread paused too long already?
      if(state->excess_delay > time_to_wait) {
        // Charge the excess delay
        state->excess_delay -= time_to_wait;
        // Update the local delay count
        state->delay_count = delays;
        
      } else {
        // Use any available excess delay
        time_to_wait -= state->excess_delay;
        // Pause and record any *new* excess delay
        state->sampler.stop();
        state->excess_delay = wait(time_to_wait) - time_to_wait;
        state->sampler.start();
        // Update the local delay count
        state->delay_count = delays;
      }
    }
    
  } else {
    // Just skip ahead on delays if there isn't an experiment running
    state->delay_count = _delays;
    state->excess_delay = 0;
  }
}

void profiler::process_samples(thread_state::ref& state) {
  // Stop sampling
  //state->sampler.stop();
  
  for(perf_event::record r : state->sampler) {
    if(r.is_sample()) {
      _samples.fetch_add(1, memory_order_relaxed);
      
      // Find the line that contains this sample
      line* sampled_line = find_line(r);
      if(sampled_line) {
        sampled_line->add_sample();
        _next_line.store(sampled_line, memory_order_relaxed);
      }
      
      if(_experiment_active) {
        // Add a delay if the sample is in the selected line
        if(sampled_line == _selected_line)
          state->delay_count++;
        
      } else if(sampled_line != nullptr && _next_line.load(memory_order_relaxed) == nullptr) {
        _next_line.store(sampled_line, memory_order_relaxed);
      }
    }
  }
  
  add_delays(state);
  
  // Resume sampling
  //state->sampler.start();
}

/**
 * Entry point for the profiler thread
 */
void* profiler::start_profiler_thread(void* arg) {
  spinlock* l = (spinlock*)arg;
  profiler::get_instance().profiler_thread(*l);
  real::pthread_exit(nullptr);
}

void profiler::samples_ready(int signum, siginfo_t* info, void* p) {
  auto state = thread_state::get(siglock::signal_context);
  if(state) {
    // Process all available samples
    profiler::get_instance().process_samples(state);
  }
}

void profiler::on_error(int signum, siginfo_t* info, void* p) {
  if(signum == SIGSEGV) {
    fprintf(stderr, "Segmentation fault at %p\n", info->si_addr);
  } else if(signum == SIGABRT) {
    fprintf(stderr, "Aborted!\n");
  } else {
    fprintf(stderr, "Signal %d at %p\n", signum, info->si_addr);
  }

  void* buf[256];
  int frames = backtrace(buf, 256);
  char** syms = backtrace_symbols(buf, frames);

  for(int i=0; i<frames; i++) {
    fprintf(stderr, "  %d: %s\n", i, syms[i]);
  }

  real::_exit(2);
}
