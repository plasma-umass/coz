#include "profiler.h"

#include <asm/unistd.h>
#include <execinfo.h>
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
#include "output.h"
#include "perf.h"
#include "spinlock.h"
#include "support.h"
#include "util.h"

using namespace causal_support;
using namespace std;

void on_error(int, siginfo_t*, void*);
void samples_ready(int, siginfo_t*, void*);

void profiler::include_file(const string& filename, uintptr_t load_address) {
  PREFER(_map.process_file(filename, load_address))
    << "Failed to locate debug version of " << filename;
}

void profiler::register_counter(Counter* c) {
  _out->add_counter(c);
}
  
/**
 * Set up the profiling environment and start the main profiler thread
 * argv, then initialize the profiler.
 */
void profiler::startup(const string& output_filename,
             const vector<string>& source_progress_names,
             const string& fixed_line_name) {
  
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
  
  // If a non-empty fixed line was provided, attempt to locate it
  if(fixed_line_name != "") {
    _fixed_line = _map.find_line(fixed_line_name);
    PREFER(_fixed_line) << "Fixed line \"" << fixed_line_name << "\" was not found.";
  }

  // Create the profiler output object
  _out = new output(output_filename);

  // Create breakpoint-based progress counters for all the lines specified via command-line
  for(const string& line_name : source_progress_names) {
    shared_ptr<line> l = _map.find_line(line_name);
    if(l) {
      WARNING << "Found line \"" << line_name << "\" but breakpoint placement hasn't been implemented for lines.";
      // TODO: Place breakpoint-based counter
      // Old code was:
      // registerCounter(new PerfCounter(ProgressCounter, b->getInterval().getBase(), name.c_str()));
    } else {
      WARNING << "Progress line \"" << line_name << "\" was not found.";
    }
  }
  
  // Log the start of this execution
  _out->startup(SamplePeriod);
  
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
    
    // Log the end of this execution
    _out->shutdown();
    delete _out;
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
    auto state = profiler::get_thread_state(siglock::thread_context);
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
    auto state = profiler::get_thread_state(siglock::thread_context);
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
  auto state = profiler::get_thread_state(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in snapshot_delays()";
  state->snapshot = _global_delays.load();
}

void profiler::skip_delays() {
  auto state = profiler::get_thread_state(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in skip_delays()";
  
  size_t current_global_delays = _global_delays.load();
  
  REQUIRE(current_global_delays >= state->snapshot) << "Global delay count should never decrease!";
  
  state->delay_count += current_global_delays - state->snapshot;
}

void profiler::catch_up() {
  auto state = profiler::get_thread_state(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in catch_up()";
  
  // Catch up on delays before unblocking any threads
  add_delays(state);
  
  /*size_t global_delay_count = _global_delays.load();
  if(state->delay_count < global_delay_count) {
    size_t wait_time = (global_delay_count - state->delay_count) * _delay_size;
    INFO << "Waiting for " << wait_time << "ns";
    state->excess_delay += wait(wait_time) - wait_time;
  }*/
}

void profiler::begin_sampling() {
  auto state = profiler::get_thread_state(siglock::thread_context);
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
  //state->sampler.set_ready_signal(SampleSignal);
  state->process_timer.start_interval(SamplePeriod * SampleWakeupCount);
  state->sampler.start();
}

void profiler::end_sampling() {
  auto state = profiler::get_thread_state(siglock::thread_context);
  REQUIRE(state) << "Unable to acquire exclusive access to thread state in end_sampling()";
  
  process_samples(state);
  
  state->sampler.stop();
  state->sampler.close();
}

void profiler::process_samples(thread_state::ref& state) {
  // Stop sampling
  state->sampler.stop();
  
  for(perf_event::record r : state->sampler) {
    if(r.is_sample()) {
      // Find the line that contains this sample
      shared_ptr<line> l = _map.find_line(r.get_ip());
    
      // Load the selected line
      line* current_line = _selected_line.load();
    
      // If there isn't a currently selected line, try to start a new round
      if(!current_line) {
        // Is this sample in a known line?
        if(l) {
          line* expected = nullptr;
          // Try to set the selected line and start a new round
          if(_selected_line.compare_exchange_strong(expected, l.get())) {
            // The swap succeeded! Also update the sentinel to keep reference counts accurate
            //_sentinel_selected_line = l;
            current_line = l.get();
          
            // Re-initialize all counters for the next round
            _round_samples.store(0);
            _round_start_delays.store(_global_delays.load());
            
            _delay_size.store(_delay_dist(_generator) * SamplePeriod / SpeedupDivisions);
          
            // Log the start of a new speedup round
            _out->start_round(current_line);
          
          } else {
            // Another thread must have changed the selected line. Reload it and continue
            current_line = _selected_line.load();
          }
        } else {
          // Sample is in some out-of-scope code. Nothing can be done.
          return;
        }
      }
      
      // Is there a currently selected line?
      if(current_line) {
        // Yes. There is an active speedup round
      
        // Is this sample in the selected line?
        if(l.get() == current_line) {
          // This thread can skip a delay (possibly one it adds to global_delays below)
          state->delay_count++;
        }
      
        // Is this the final sample in the round?
        if(++_round_samples == MinRoundSamples) {
          // Log the end of the speedup round
          _out->end_round(_global_delays.load() - _round_start_delays.load(), _delay_size.load());
        
          // Clear the selected line
          _selected_line.store(nullptr);
          // Also clear the sentinel to keep an accurate reference count
          //_sentinel_selected_line.reset();
        }
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
    if(to_add * delay_size > 1000000000) {
      fprintf(stderr, "Adding %fs of delay\n"
                      "\tdelay size is: %lu\n"
                      "\tlocal delay count is: %lu\n"
                      "\tglobal delay count is: %lu\n\n", 
          (double)to_add * delay_size / 1000000000,
          delay_size,
          state->delay_count,
          global_delay_count);
      real::exit(2);
    }
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
      if(time_to_wait > 1000000000) {
        INFO << "Waiting for " << ((double)time_to_wait / 1000000000) << "s";
      }
      // Pause, and record any new excess delay
      state->excess_delay = wait(time_to_wait) - time_to_wait;
      // Update the delay count
      state->delay_count = global_delay_count;
    }
  }
}

void profiler::samples_ready(int signum, siginfo_t* info, void* p) {
  auto state = profiler::get_thread_state(siglock::signal_context);
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
