#include "profiler.h"

#include <asm/unistd.h>
#include <execinfo.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

void on_error(int, siginfo_t*, void*);
void samples_ready(int, siginfo_t*, void*);

void profiler::register_counter(counter* c) {
  _output_lock.lock();
  _counters.push_back(c);
  _output_lock.unlock();
};
  
/**
 * Set up the profiling environment and start the main profiler thread
 * argv, then initialize the profiler.
 */
void profiler::startup(const string& output_filename,
                       shared_ptr<line> fixed_line,
                       int fixed_speedup) {
  
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
  
  // If a non-empty fixed line was provided, set it
  if(fixed_line) {
    _fixed_line = fixed_line;
  }
  
  // If the speedup amount is in bounds, set a fixed delay size
  if(fixed_speedup >= 0 && fixed_speedup <= 100) {
    _fixed_delay_size = SamplePeriod * fixed_speedup / 100;
  }

  // Create the profiler output object
  _output.open(output_filename, ios_base::app);
  
  // Log the start of this execution
  _output << "startup\t"
          << "time=" << get_time() << "\t"
          << "sample-period=" << SamplePeriod << "\n";
  
  // Begin sampling in the main thread
  begin_sampling();
}

/**
 * Flush output and terminate the profiler
 */
void profiler::shutdown() {
  if(_shutdown_run.test_and_set() == false) {
    // Stop sampling in the main thread
    end_sampling();
    
    // Force the end of the current experiment, if one is running
    end_experiment();
    
    // Log the end of this execution
    _output_lock.lock();
    _output << "shutdown\ttime=" << get_time() << "\n";
    
    for(const auto& file_entry : memory_map::get_instance().files()) {
      for(const auto& line_entry : file_entry.second->lines()) {
        shared_ptr<line> l = line_entry.second;
        if(l->get_samples() > 0) {
          _output << "samples\t"
                  << "line=" << l << "\t"
                  << "count=" << l->get_samples() << "\n";
        }
      }
    }
    
    _output.close();
    _output_lock.unlock();
  }
}

struct thread_start_arg {
  thread_fn_t _fn;
  void* _arg;
  size_t _parent_delay_count;
  size_t _parent_excess_delay;
  
  thread_start_arg(thread_fn_t fn, void* arg, size_t c, size_t t) :
      _fn(fn), _arg(arg), _parent_delay_count(c), _parent_excess_delay(t) {}
};

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

void profiler::handle_pthread_exit(void* result) {
  end_sampling();
  real::pthread_exit(result);
}

void profiler::snapshot_delays() {
  auto state = thread_state::get(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in snapshot_delays()";
  state->global_delay_snapshot = _global_delays.load();
  state->local_delay_snapshot = state->delay_count;
}

void profiler::skip_delays() {
  auto state = thread_state::get(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in skip_delays()";
  
  // Count the number of delays that occurred since the snapshot
  size_t missed_delays = _global_delays.load() - state->global_delay_snapshot;
  
  // Skip over the missed delays. Add to the saved local count to prevent double-counting
  state->delay_count = state->local_delay_snapshot + missed_delays;
}

void profiler::catch_up() {
  auto state = thread_state::get(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in catch_up()";
  
  // Catch up on delays before unblocking any threads
  add_delays(state);
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
    .wakeup_events = SampleWakeupCount, // This is ignored on linux 3.13 (why?)
    .exclude_idle = 1,
    .exclude_kernel = 1,
    .disabled = 1
  };
  
  // Create this thread's perf_event sampler and start sampling
  state->sampler = perf_event(pe);
  state->process_timer = timer(SampleSignal);
  state->process_timer.start_interval(SamplePeriod * SampleWakeupCount);
  state->sampler.start();
}

void profiler::end_sampling() {
  auto state = thread_state::get(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in end_sampling()";
  
  process_samples(state);
  add_delays(state);
  
  state->sampler.stop();
  state->sampler.close();
}

/// Is the program ready to start an experiment? There may already be one running...
bool profiler::experiment_ready() {
  // Only start an experiment if there is at least one performance counter
  return _counters.size() > 0;
}

/// Is there currently an experiment running?
bool profiler::experiment_running() {
  // If there is a selected line, the experiment is running
  return _selected_line.load() != nullptr;
}

/// Is the current experiment ready to end?
bool profiler::experiment_finished() {
  // Increment the count of samples this experiment
  size_t num = ++_experiment_samples;
  
  // Every so often, check if counters have changed
  if(num % ExperimentMinSamples == 0) {
    // If there haven't been enough delays, we're not finished
    if(_global_delays - _round_start_delays < ExperimentMinDelays && num < ExperimentAbortThreshold)
      return false;
    
    for(size_t i=0; i<_prev_counter_values.size(); i++) {
      if(_counters[i]->get_count() - _prev_counter_values[i] < ExperimentMinCounterChange)
        return false;
    }
    
    return true;
    
  } else {
    return false;
  }
}

/// Start a new performance experiment
void profiler::start_experiment(shared_ptr<line> next_line, size_t delay_size) {
  ASSERT(experiment_ready()) << "Not ready to start an experiment!";

  line* expected = nullptr;
  if(_selected_line.compare_exchange_strong(expected, next_line.get())) {
    _delay_size.store(delay_size);
    _experiment_samples.store(0);
    _round_start_delays.store(_global_delays.load());
    
    // Log the start of a new speedup round
    _output_lock.lock();
    _output << "start-experiment\t"
            << "line=" << next_line << "\t"
            << "time=" << get_time() << "\t"
            << "selected-line-samples=" << next_line->get_samples() << "\t"
            << "global-delays=" << _global_delays.load() << "\n";
    
    _prev_counter_values.clear();
    
    for(const counter* c : _counters) {
      _output << *c;
      _prev_counter_values.push_back(c->get_count());
    }
    _output_lock.unlock();
  }
}

/// End the current performance experiment
void profiler::end_experiment() {
  line* old_selected_line = _selected_line.exchange(nullptr);
  if(old_selected_line != nullptr) {
    // Log the end of the speedup round
    _output_lock.lock();
    _output << "end-experiment\t"
            << "time=" << get_time() << "\t"
            << "delay-size=" << _delay_size.load() << "\t"
            << "global-delays=" << _global_delays.load() << "\t"
            << "selected-line-samples=" << old_selected_line->get_samples() << "\n";

    for(const counter* c : _counters) {
      _output << *c;
    }
    _output_lock.unlock();
  }
}

shared_ptr<line> profiler::find_containing_line(perf_event::record& sample) {
  if(!sample.is_sample())
    return shared_ptr<line>();
  
  // Check if the sample occurred in known code
  shared_ptr<line> l = memory_map::get_instance().find_line(sample.get_ip());
  if(l)
    return l;
  
  // Walk the callchain
  for(uint64_t pc : sample.get_callchain()) {
    // Need to subtract one. PC is the return address, but we're looking for the callsite.
    l = memory_map::get_instance().find_line(pc-1);
    if(l) {
      return l;
    }
  }
  
  // No hits. Return null
  return shared_ptr<line>();
}

void profiler::process_samples(thread_state::ref& state) {
  // Stop sampling
  state->sampler.stop();
  
  for(perf_event::record r : state->sampler) {
    if(r.is_sample()) {
      // Find the line that contains this sample
      shared_ptr<line> sampled_line = find_containing_line(r);
      
      if(sampled_line) {
        sampled_line->add_sample();
      }
      
      if(!experiment_running() && experiment_ready()) {
        shared_ptr<line> next_selected_line = sampled_line;
        if(_fixed_line)
          next_selected_line = _fixed_line;
        
        if(next_selected_line && next_selected_line->get_line() != 358) {
          size_t next_delay_size;
          if(_fixed_delay_size != -1)
            next_delay_size = _fixed_delay_size;
          else
            next_delay_size = _delay_dist(_generator) * SamplePeriod / SpeedupDivisions;
          
          // Start a new experiment with the new line and delay size
          start_experiment(next_selected_line, next_delay_size);
        }
      } else if(sampled_line) {
        // If the sampled line is the selected line, add a delay
        if(sampled_line.get() == _selected_line.load())
          state->delay_count++;
        
        // Have all conditions for the end of the experiment been met? If so, finish the experiment.
        if(experiment_finished())
          end_experiment();
      }
    }
  }
  
  add_delays(state);
  
  // Resume sampling
  state->sampler.start();
}

void profiler::add_delays(thread_state::ref& state) {
  // Take snapshots of global and local delay information
  size_t global_delay_count = _global_delays.load();
  size_t delay_size = _delay_size.load();

  // If this thread has more delays + visits than the global delay count, update the global count
  if(state->delay_count > global_delay_count) {
    size_t to_add = state->delay_count - global_delay_count;
    _global_delays += (state->delay_count - global_delay_count);
    
  } else if(state->delay_count < global_delay_count) {
    size_t time_to_wait = (global_delay_count - state->delay_count) * delay_size;
    
    // If this thread has any extra pause time, is it larger than the required delay size?
    if(state->excess_delay > time_to_wait) {
      // Remove the time to wait from the excess delays and jump ahead on local delays
      state->excess_delay -= time_to_wait;
      state->delay_count = global_delay_count;
    } else {
      // Use all the local excess delay time to reduce the pause time
      time_to_wait -= state->excess_delay;
      // Pause, and record any new excess delay
      state->excess_delay = wait(time_to_wait) - time_to_wait;
      // Update the delay count
      state->delay_count = global_delay_count;
    }
  }
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
