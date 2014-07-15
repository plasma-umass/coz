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
thread_local size_t local_delays;

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
    .sa_sigaction = samples_ready,
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
}

/**
 * Flush output and terminate the profiler
 */
void profiler::shutdown() {
  if(_shutdown_run.test_and_set() == false) {
    delete _out;
  }
}
  
void profiler::thread_startup(size_t parent_round, size_t parent_delays) {
  local_round = parent_round;
  local_delays = parent_delays;
  
  struct perf_event_attr pe = {
    .type = PERF_TYPE_HARDWARE,
    .config = PERF_COUNT_HW_CPU_CYCLES,
    .sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_CALLCHAIN,
    .sample_period = SamplePeriod,
    .wakeup_events = SampleWakeupCount, // This is ignored on linux 3.13 (why?)
    .exclude_idle = 1,
    .exclude_kernel = 1,
    .disabled = 1
  };
  
  // Set up the thread-local perf_event sampler
  perf_event* s = new perf_event(pe);
  s->set_ready_signal(SampleSignal);
  s->start();
  
  // Place the sampler in the shared atomic pointer
  sampler.store(s);
}

void profiler::thread_shutdown() {
  // TODO: catch up on delays before exiting
  
  // Claim the sampler object and free it
  perf_event* s = sampler.exchange(nullptr);
  delete s;
}

size_t profiler::get_local_round() {
  return local_round;
}

size_t profiler::get_local_delays() {
  return local_delays;
}

void samples_ready(int signum, siginfo_t* info, void* p) {
  // Attempt to claim the sampler
  perf_event* s = sampler.exchange(nullptr);
  
  // If the sampler was not available to be claimed, try again later
  if(!s)
    return;
  
  // Stop sampling
  s->stop();
  
  s->process([](const perf_event::record& r) {
    if(r.is_sample()) {
      //fprintf(stderr, "Sample at %p\n", (void*)r.get_ip());
    }
  });
  
  // Resume sampling
  s->start();
  
  // Return the sampler to the shared atomic pointer
  sampler.exchange(s);
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
