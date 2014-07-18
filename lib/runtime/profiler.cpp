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

/// Thread-local round number
thread_local size_t local_round;

/// Thread-local delay count for the current round
thread_local size_t local_delay_count;

/// Amount of extra delay time added to this thread (because nanosleep may return late)
thread_local size_t local_excess_delay;

/// A snapshot of the global delay count taken just before (possibly) blocking on a mutex/cond_var
thread_local size_t global_delay_snapshot;

// Thread-local perf_event sampler
thread_local atomic<perf_event*> sampler(nullptr);

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
  real::sigaction()(SampleSignal, &sa, nullptr);
  
  // Set up handlers for errors
  sa = {
    .sa_sigaction = on_error,
    .sa_flags = SA_SIGINFO
  };
  real::sigaction()(SIGSEGV, &sa, nullptr);
  
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
  start_sampler();
}

/**
 * Flush output and terminate the profiler
 */
void profiler::shutdown() {
  if(_shutdown_run.test_and_set() == false) {
    // Stop sampling in the main thread
    stop_sampler();
    
    // Log the end of this execution
    _out->shutdown();
    delete _out;
  }
}

struct thread_start_arg {
  thread_fn_t _fn;
  void* _arg;
  size_t _parent_round;
  size_t _parent_delay_count;
  size_t _parent_excess_delay;
  
  thread_start_arg(thread_fn_t fn, void* arg, size_t r, size_t c, size_t t) :
      _fn(fn), _arg(arg), _parent_round(r), _parent_delay_count(c), _parent_excess_delay(t) {}
};

void* profiler::start_thread(void* p) {
  thread_start_arg* arg = reinterpret_cast<thread_start_arg*>(p);
  
  // Copy over the round, delay count, and delay time from the parent thread
  local_round = arg->_parent_round;
  local_delay_count = arg->_parent_delay_count;
  local_excess_delay = arg->_parent_excess_delay;
  
  // Make local copies of the function and argument before freeing the arg wrapper
  thread_fn_t real_fn = arg->_fn;
  void* real_arg = arg->_arg;
  delete arg;
  
  // Start the sampler for this thread
  profiler::get_instance().start_sampler();
  
  // Run the real thread function
  void* result = real_fn(real_arg);
  
  // Always exit via pthread_exit
  pthread_exit(result);
}

int profiler::handle_pthread_create(pthread_t* thread,
                                    const pthread_attr_t* attr,
                                    thread_fn_t fn,
                                    void* arg) {
  // Allocate a struct to pass as an argument to the new thread
  thread_start_arg* new_arg = new thread_start_arg(fn, arg, 
                                                   local_round,
                                                   local_delay_count,
                                                   local_excess_delay);
  // Create a wrapped thread and pass in the above argument
  return real::pthread_create()(thread, attr, profiler::start_thread, new_arg);
}

void profiler::handle_pthread_exit(void* result) {
  stop_sampler();
  real::pthread_exit()(result);
}

void profiler::snapshot_delays() {
  REQUIRE(global_delay_snapshot == 0) << "Nested snapshots shouldn't happen!";
  global_delay_snapshot = _global_delays.load();
}

void profiler::skip_delays() {
  size_t current_global_delays = _global_delays.load();
  // If delays have occurred since the snapshot was taken, skip them
  if(current_global_delays > global_delay_snapshot) {
    local_delay_count += current_global_delays - global_delay_snapshot;
  } else if(current_global_delays < global_delay_snapshot) {
    // We must be in a new round
    // TODO: Should update round here, then skip any delays from the beginning of the round.
  }
  // Clear the snapshot
  global_delay_snapshot = 0;
}

void profiler::catch_up() {
  // Process samples and catch up on delays before unblocking any threads
  PREFER(process_samples()) << "Unable to catch up...";
}

void profiler::start_sampler() {
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
  perf_event* s = new perf_event(pe);
  s->set_ready_signal(SampleSignal);
  s->start();
  
  // Place the sampler in the shared atomic pointer
  sampler.store(s);
}

void profiler::stop_sampler() {
  // Catch up on delays before finishing. Keep trying until success
  while(!process_samples()) {
    __asm__("pause");
  }
  
  // Claim the sampler object and free it
  perf_event* s = sampler.exchange(nullptr);
  delete s;
}

void profiler::call_process_one_sample(const perf_event::record& r) {
  profiler::get_instance().process_one_sample(r);
}

void profiler::process_one_sample(const perf_event::record& r) {
  if(r.is_sample()) {
    // Find the line that contains this sample
    shared_ptr<line> l = _map.find_line(r.get_ip());
    
    // Load the current round number and selected line
    size_t current_round = _global_round.load();
    line* current_line = _selected_line.load();
    
    // If there isn't a currently selected line, try to start a new round
    if(!current_line) {
      // Is this sample in a known line?
      if(l) {
        // Try to set the selected line and start a new round
        if(_selected_line.compare_exchange_weak(current_line, l.get())) {
          // The swap succeeded! Also update the sentinel to keep reference counts accurate
          _sentinel_selected_line = l;
          
          // Update the round number (and the local copy of it)
          current_round = ++_global_round;
          
          // Clear the count of samples this round
          _round_samples.store(0);
          
          // Update the local round counter
          local_round = current_round;
          
          // Update the local copy of the current line
          current_line = l.get();
          
          // Clear the global and local delay counts
          _global_delays = 0;
          local_delay_count = 0;
          
          // Clear the count of extra delay time from nanosleep() returning late
          local_excess_delay = 0;
          
          // Generate a new random delay size
          _delay_size.store(_delay_dist(_generator) * SamplePeriod / SpeedupDivisions);
          
          // Log the start of a new speedup round
          _out->start_round(current_line);
          
        } else {
          // Another thread must have changed the round and selected line. Reload them and continue.
          current_round = _global_round.load();
          current_line = _selected_line.load();
        }
      } else {
        // Sample is in some out-of-scope code. Nothing can be done with this sample.
        return;
      }
    }
      
    // Is there a currently selected line?
    if(current_line) {
      // Yes. There is an active speedup round
      
      // Does this thread's round number match the global round?
      if(current_round != local_round) {
        // If not, clear the local delay count and advance to the next round
        local_round = current_round;
        local_delay_count = 0;
        local_excess_delay = 0;
      }
      
      // Is this sample in the selected line?
      if(l.get() == current_line) {
        // This thread can skip a delay (possibly one it adds to global_delays below)
        local_delay_count++;
      }
      
      // Is this the final sample in the round?
      if(++_round_samples == MinRoundSamples) {
        // Log the end of the speedup round
        _out->end_round(_global_delays.load(), _delay_size.load());
        
        // Clear the selected line
        _selected_line.store(nullptr);
        // Also clear the sentinel to keep an accurate reference count
        _sentinel_selected_line.reset();
      }
    }
  }
}

bool profiler::process_samples() {
  // Attempt to claim the sampler
  perf_event* s = sampler.exchange(nullptr);
  // If the sampler is unavailable, give up
  if(!s) return false;
  
  // Stop sampling
  s->stop();
  
  s->process(profiler::call_process_one_sample);
  
  // Take snapshots of global and local delay information
  size_t global_delay_count = _global_delays.load();
  size_t delay_size = _delay_size.load();

  // If this thread has more delays + visits than the global delay count, update the global count
  if(local_delay_count > global_delay_count) {
    _global_delays += (local_delay_count - global_delay_count);
    
  } else if(local_delay_count < global_delay_count) {
    size_t time_to_wait = (global_delay_count - local_delay_count) * delay_size;
    // If this thread has any extra pause time, is it larger than the required delay size?
    if(local_excess_delay > time_to_wait) {
      // Remove the time to wait from the excess delays and jump ahead on local delays
      local_excess_delay -= time_to_wait;
      local_delay_count = global_delay_count;
    } else {
      // Use all the local excess delay time to reduce the pause time
      time_to_wait -= local_excess_delay;
      local_excess_delay = 0;
      // Pause, and record any new excess delay
      local_excess_delay = wait(time_to_wait) - time_to_wait;
    }
  }
  
  // Resume sampling
  s->start();
  // Release the sampler
  sampler.exchange(s);
  return true;
}

void profiler::samples_ready(int signum, siginfo_t* info, void* p) {
  // Process all available samples
  profiler::get_instance().process_samples();
}

void on_error(int signum, siginfo_t* info, void* p) {
  fprintf(stderr, "Signal %d at %p\n", signum, info->si_addr);

  void* buf[256];
  int frames = backtrace(buf, 256);
  char** syms = backtrace_symbols(buf, frames);

  for(int i=0; i<frames; i++) {
    fprintf(stderr, "  %d: %s\n", i, syms[i]);
  }

  real::_exit()(2);
}
