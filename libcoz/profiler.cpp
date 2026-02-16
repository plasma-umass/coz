/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include "profiler.h"

#ifndef __APPLE__
  #include <asm/unistd.h>
#else
  // macOS doesn't have gettid(), provide implementation
  #include <pthread.h>
  #include <mach/mach.h>
  #include "perf_macos.h"
  static inline pid_t gettid() {
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return (pid_t)tid;
  }
  // Declare original functions from mac_interpose.cpp.
  // On macOS, dlsym returns DYLD-interposed versions, so we must use
  // __asm-bound originals for internal thread/signal management.
  extern "C" int coz_orig_pthread_create(pthread_t*, const pthread_attr_t*, void*(*)(void*), void*);
  extern "C" int coz_orig_pthread_join(pthread_t, void**);
  extern "C" int coz_orig_sigaction(int, const struct sigaction*, struct sigaction*);
  extern "C" int coz_orig_sigprocmask(int, const sigset_t*, sigset_t*);
#endif
#include <execinfo.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "inspect.h"
#include "perf.h"
#include "progress_point.h"
#include "util.h"

#include "ccutil/log.h"
#include "ccutil/spinlock.h"
#include "ccutil/timer.h"

using namespace std;

// Diagnostic counters for delay application (macOS debugging)
#ifdef __APPLE__
static std::atomic<size_t> g_delays_applied{0};
static std::atomic<size_t> g_delays_skipped{0};
static std::atomic<size_t> g_delay_checks{0};
// Track nanosleep overshoot separately from the delay counters.
// On macOS, nanosleep has ~1ms granularity, so the actual sleep time
// exceeds the requested amount. This overshoot adds to wall clock time
// but is not reflected in global_delay (to prevent feedback loops).
// We subtract it from duration to correct for this bias.
static std::atomic<size_t> g_experiment_overshoot{0};
#endif

/// Escape a string for JSON output
static string json_escape(const string& s) {
  string result;
  result.reserve(s.size() + 8);
  for(char c : s) {
    switch(c) {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:   result += c; break;
    }
  }
  return result;
}

/// Get JSON-safe string representation of a line
static string line_to_json_string(const line* l) {
  auto f = l->get_file();
  return json_escape(f->get_name() + ":" + to_string(l->get_line()));
}

/// Check if a line is from the coz.h instrumentation header
static bool is_coz_header(const line* l) {
  auto f = l->get_file();
  if(!f) return false;
  const auto& name = f->get_name();
  if(name.length() < 6) return false;
  // Check for coz.h preceded by a directory separator (/ on Unix, \ on Windows)
  char sep = name[name.length() - 6];
  return (sep == '/' || sep == '\\') && name.compare(name.length() - 5, 5, "coz.h") == 0;
}

/**
 * Start the profiler
 */
void profiler::startup(const string& outfile,
                       line* fixed_line,
                       int fixed_speedup,
                       bool end_to_end) {
  // Set up the sampling signal handler.
  // On macOS, use the __asm-bound original sigaction to bypass our own
  // DYLD interposition which blocks setting handlers for coz's signals.
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = profiler::samples_ready;
  sa.sa_flags = SA_SIGINFO;
#ifdef __APPLE__
  coz_orig_sigaction(SampleSignal, &sa, nullptr);
#else
  real::sigaction(SampleSignal, &sa, nullptr);
#endif

  // Set up handlers for errors
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = on_error;
  sa.sa_flags = SA_SIGINFO;

#ifdef __APPLE__
  coz_orig_sigaction(SIGSEGV, &sa, nullptr);
  coz_orig_sigaction(SIGABRT, &sa, nullptr);
  coz_orig_sigaction(SIGBUS, &sa, nullptr);
#else
  real::sigaction(SIGSEGV, &sa, nullptr);
  real::sigaction(SIGABRT, &sa, nullptr);
  real::sigaction(SIGBUS, &sa, nullptr);
#endif

  // Save the output file name
  _output_filename = outfile;

  // Check output format (JSON is the default)
  const char* output_format = getenv("COZ_OUTPUT_FORMAT");
  if(output_format && strcmp(output_format, "json") != 0) {
    // Legacy format requested
    _json_output = false;
  }

  // If a non-empty fixed line was provided, set it
  if(fixed_line) _fixed_line = fixed_line;

  // If the speedup amount is in bounds, set a fixed delay size
  if(fixed_speedup >= 0 && fixed_speedup <= 100)
    _fixed_delay_size = SamplePeriod * fixed_speedup / 100;

  // Should end-to-end mode be enabled?
  _enable_end_to_end = end_to_end;

  // Use a spinlock to wait for the profiler thread to finish intialization
  spinlock l;
  l.lock();

  // Create the profiler thread
  VERBOSE << "Creating profiler thread...";
#ifdef __APPLE__
  // On macOS, use the original pthread_create from mac_interpose.cpp to avoid
  // recursion through the interposed function
  int rc = coz_orig_pthread_create(&_profiler_thread, nullptr, profiler::start_profiler_thread, (void*)&l);
#else
  int rc = real::pthread_create(&_profiler_thread, nullptr, profiler::start_profiler_thread, (void*)&l);
#endif
  VERBOSE << "pthread_create returned " << rc;
  REQUIRE(rc == 0) << "Failed to start profiler thread";

  // Double-lock l. This blocks until the profiler thread unlocks l
  VERBOSE << "Waiting for profiler thread to start...";
  l.lock();
  VERBOSE << "Profiler thread started, adding main thread state...";

  // Begin sampling in the main thread
  thread_state* state = add_thread();
  REQUIRE(state) << "Failed to add thread";
  VERBOSE << "Starting sampling on main thread...";
  begin_sampling(state);
  VERBOSE << "Profiler startup complete, returning to main program";
}

/**
 * Body of the main profiler thread
 */
void profiler::profiler_thread(spinlock& l) {
  VERBOSE << "Profiler thread running!";

#ifdef __APPLE__
  // Register the profiler thread so the sampling thread never suspends it
  macos_register_internal_thread(mach_thread_self());
#endif

  // Open the output file
  ofstream output;
  output.open(_output_filename, ios_base::app);
  output.rdbuf()->pubsetbuf(0, 0);
  output.setf(ios::fixed, ios::floatfield);
  output.precision(2);

  // Initialize the delay size RNG
  default_random_engine generator(get_time());
  uniform_int_distribution<size_t> delay_dist(0, ZeroSpeedupWeight + SpeedupDivisions);

  // Initialize the experiment duration
  size_t experiment_length = ExperimentMinTime;

  // Get the starting time for the profiler
  size_t start_time = get_time();

  // Log the start of this execution
  if(_json_output) {
    output << "{\"type\":\"startup\",\"time\":" << start_time << "}\n";
  } else {
    output << "startup\t"
           << "time=" << start_time << "\n";
  }

  // Unblock the main thread
  VERBOSE << "Profiler thread unlocking spinlock...";
  l.unlock();
  VERBOSE << "Profiler thread waiting for progress points...";

  // Wait until there is at least one progress point
  _throughput_points_lock.lock();
  _latency_points_lock.lock();
  while(_throughput_points.size() == 0 && _latency_points.size() == 0 && _running) {
    _throughput_points_lock.unlock();
    _latency_points_lock.unlock();
    wait(ExperimentCoolOffTime);
    _throughput_points_lock.lock();
    _latency_points_lock.lock();
  }
  _throughput_points_lock.unlock();
  _latency_points_lock.unlock();

  // Log sample counts after this many experiments (doubles each time)
  size_t sample_log_interval = 32;
  size_t sample_log_countdown = sample_log_interval;

  // Main experiment loop
  while(_running) {
    // Select a line
    line* selected;
    if(_fixed_line) {   // If this run has a fixed line, use it
      selected = _fixed_line;
    } else {            // Otherwise, wait for the next line to be selected
      selected = _next_line.load();
      while(_running && selected == nullptr) {
        wait(SamplePeriod * SampleBatchSize);
#ifdef __APPLE__
        // On macOS, must process samples here to set _next_line
        process_all_samples();
        apply_pending_delays();
#endif
        selected = _next_line.load();
      }

      // If we're no longer running, exit the experiment loop
      if(!_running) break;
    }

    // Store the globally-visible selected line
    _selected_line.store(selected);

    // Choose a delay size
    size_t delay_size;
    if(_fixed_delay_size >= 0) {
      delay_size = _fixed_delay_size;
    } else {
      size_t r = delay_dist(generator);
      if(r <= ZeroSpeedupWeight) {
        delay_size = 0;
      } else {
        delay_size = (r - ZeroSpeedupWeight) * SamplePeriod / SpeedupDivisions;
      }
    }

    _delay_size.store(delay_size);

    // Save throughput point values at the start of the experiment
    vector<unique_ptr<throughput_point::saved>> saved_throughput_points;
    _throughput_points_lock.lock();
    for(pair<const std::string, throughput_point*>& p : _throughput_points) {
      saved_throughput_points.emplace_back(p.second->save());
    }
    _throughput_points_lock.unlock();

    // Save latency point values at the start of the experiment
    vector<unique_ptr<latency_point::saved>> saved_latency_points;
    _latency_points_lock.lock();
    for(pair<const std::string, latency_point*>& p : _latency_points) {
      saved_latency_points.emplace_back(p.second->save());
    }
    _latency_points_lock.unlock();

    // Sync all non-blocked threads' local_delay to global_delay before starting.
    // This prevents stale residual from a previous experiment from causing threads
    // to skip delays (local > global branch in add_delays).
    // Skip blocked threads — their pre_block/post_block mechanism handles accounting.
    {
      size_t current_global = _global_delay.load();
      _thread_states.for_each([current_global](pid_t tid, thread_state* state) {
        if(!state->is_blocked.load()) {
          state->local_delay.store(current_global);
        }
      });
    }

#ifdef __APPLE__
    // Reset overshoot counter for this experiment
    g_experiment_overshoot.store(0, std::memory_order_relaxed);
#endif

    // Snapshot timing AFTER setup (saving progress points, syncing delays) to
    // exclude variable setup overhead from the elapsed time measurement.
    // Previously this was before setup, causing 0% baseline experiments to have
    // inflated durations when setup was slow.
    size_t start_time = get_time();
    size_t starting_samples = selected->get_samples();
    size_t starting_delay_time = _global_delay.load();

    // Tell threads to start the experiment
    _experiment_active.store(true);

    // Wait until the experiment ends, or until shutdown if in end-to-end mode
    if(_enable_end_to_end) {
      while(_running) {
        wait(SamplePeriod * SampleBatchSize);
#ifdef __APPLE__
        // On macOS, process samples from profiler thread since signal-based
        // processing doesn't work when threads are blocked (e.g., in join)
        process_all_samples();
        apply_pending_delays();
#endif
      }
    } else {
#ifdef __APPLE__
      // On macOS, break the wait into chunks and process samples periodically.
      // Use wall clock time to bound the experiment duration, since overhead
      // from process_all_samples() and apply_pending_delays() can accumulate
      // significantly (e.g., thread suspension in the sampling thread, lock
      // contention) causing experiments to run 3-4x longer than intended.
      {
        size_t experiment_deadline = get_time() + experiment_length;
        size_t chunk = SamplePeriod * SampleBatchSize;
        while(_running && get_time() < experiment_deadline) {
          wait(chunk);
          process_all_samples();
          apply_pending_delays();
        }
      }
#else
      wait(experiment_length);
#endif
    }

    // Compute experiment parameters
    float speedup = (float)delay_size / (float)SamplePeriod;
    size_t end_global_delay = _global_delay.load();
    size_t end_time = get_time();
    size_t experiment_delay = end_global_delay - starting_delay_time;
    size_t elapsed = end_time - start_time;
#ifdef __APPLE__
    // On macOS, nanosleep has ~1ms granularity. The overshoot (actual sleep
    // time minus requested time) adds to wall clock time but isn't tracked
    // in global_delay (to prevent feedback loops). Subtract it from duration
    // to correct for this systematic bias.
    size_t overshoot = g_experiment_overshoot.load(std::memory_order_relaxed);
    size_t duration = elapsed - experiment_delay - overshoot;
#else
    size_t duration = elapsed - experiment_delay;
#endif
    size_t selected_samples = selected->get_samples() - starting_samples;

    // Keep a running count of the minimum delta over all progress points
    size_t min_delta = std::numeric_limits<size_t>::max();

    // Compute min_delta first to decide whether to emit this experiment
    for(const auto& s : saved_throughput_points) {
      size_t delta = s->get_delta();
      if(delta < min_delta) min_delta = delta;
    }
    for(const auto& s : saved_latency_points) {
      size_t begin_delta = s->get_begin_delta();
      size_t end_delta = s->get_end_delta();
      if(begin_delta < min_delta) min_delta = begin_delta;
      if(end_delta < min_delta) min_delta = end_delta;
    }

    // Only emit experiment data when we have enough progress point visits.
    // Low-delta experiments (e.g., from warmup, end-of-benchmark, or boundary
    // effects) have unreliable throughput measurements that corrupt the baseline.
    if(min_delta >= ExperimentTargetDelta) {
      if(_json_output) {
        output << "{\"type\":\"experiment\",\"selected\":\"" << line_to_json_string(selected) << "\","
               << "\"speedup\":" << speedup << ","
               << "\"duration\":" << duration << ","
               << "\"selected_samples\":" << selected_samples << "}\n";
      } else {
        output << "experiment\t"
               << "selected=" << selected << "\t"
               << "speedup=" << speedup << "\t"
               << "duration=" << duration << "\t"
               << "selected-samples=" << selected_samples << "\n";
      }

      for(const auto& s : saved_throughput_points) {
        size_t delta = s->get_delta();
        if(_json_output) {
          output << "{\"type\":\"throughput_point\",\"name\":\"" << json_escape(s->get_name()) << "\","
                 << "\"delta\":" << delta << "}\n";
        } else {
          s->log(output);
        }
      }

      for(const auto& s : saved_latency_points) {
        if(_json_output) {
          output << "{\"type\":\"latency_point\",\"name\":\"" << json_escape(s->get_name()) << "\","
                 << "\"arrivals\":" << s->get_begin_delta() << ","
                 << "\"departures\":" << s->get_end_delta() << ","
                 << "\"difference\":" << s->get_difference() << "}\n";
        } else {
          s->log(output);
        }
      }
    }

    // Lengthen the experiment if the min_delta is too small
    if(min_delta < ExperimentTargetDelta) {
      experiment_length *= 2;
      // Cap to prevent experiments from consuming too much of the benchmark's runtime.
      // With very long experiments, we get fewer data points and higher noise.
      size_t max_length = (size_t)ExperimentMinTime * 16;  // 8 seconds
      if(experiment_length > max_length) experiment_length = max_length;
    } else if(min_delta > ExperimentTargetDelta*2 && experiment_length >= ExperimentMinTime*2) {
      experiment_length /= 2;
    }

#ifdef __APPLE__
    // Log delay application statistics (macOS debugging)
    size_t checks = g_delay_checks.exchange(0, std::memory_order_relaxed);
    size_t applied = g_delays_applied.exchange(0, std::memory_order_relaxed);
    size_t skipped = g_delays_skipped.exchange(0, std::memory_order_relaxed);
    VERBOSE << "Delay stats: checks=" << checks
            << ", applied=" << applied << ", skipped=" << skipped
            << ", overshoot=" << overshoot / 1000000 << "ms"
            << ", speedup=" << (speedup * 100) << "%"
            << ", min_delta=" << min_delta;
#endif

    output.flush();

    // Clear the next line, so threads will select one
    _next_line.store(nullptr);

    // End the experiment
    _experiment_active.store(false);

    // Log samples after a while, then double the countdown
    if(--sample_log_countdown == 0) {
      log_samples(output, start_time);
      if(sample_log_interval < 20) {
        sample_log_interval *= 2;
      }
      sample_log_countdown = sample_log_interval;
    }

    // Cool off before starting a new experiment, unless the program is exiting
    if(_running) wait(ExperimentCoolOffTime);
  }

  // Log the sample counts on exit
  log_samples(output, start_time);

  output.flush();
  output.close();
}

void profiler::log_samples(ofstream& output, size_t start_time) {
  // Log total runtime for phase correction
  if(_json_output) {
    output << "{\"type\":\"runtime\",\"time\":" << (get_time() - start_time) << "}\n";
  } else {
    output << "runtime\t"
           << "time=" << (get_time() - start_time) << "\n";
  }

  // Log sample counts for all observed lines
  for(const auto& file_entry : memory_map::get_instance().files()) {
    for(const auto& line_entry : file_entry.second->lines()) {
      shared_ptr<line> l = line_entry.second;
      if(l->get_samples() > 0) {
        if(_json_output) {
          output << "{\"type\":\"samples\",\"location\":\"" << line_to_json_string(l.get()) << "\","
                 << "\"count\":" << l->get_samples() << "}\n";
        } else {
          output << "samples\t"
                 << "location=" << l << "\t"
                 << "count=" << l->get_samples() << "\n";
        }
      }
    }
  }
}

/**
 * Terminate the profiler thread, then exit
 */
void profiler::shutdown() {
  if(_shutdown_run.test_and_set() == false) {
#ifdef __APPLE__
    // Stop the macOS sampling thread first so no more samples are produced
    // and no more pthread_kill() calls happen from apply_pending_delays()
    macos_sampling_stop();
#endif

    // "Signal" the profiler thread to stop
    _running.store(false);

    // Join with the profiler thread
#ifdef __APPLE__
    // Use __asm-bound original to bypass DYLD interposition
    coz_orig_pthread_join(_profiler_thread, nullptr);
#else
    real::pthread_join(_profiler_thread, nullptr);
#endif

    // Clean up main thread state last
    end_sampling();
  }
}

thread_state* profiler::add_thread() {
  pid_t tid = gettid();
  thread_state* inserted = _thread_states.insert(tid);
  if (inserted != nullptr) {
    _num_threads_running += 1;
    VERBOSE << "Registered thread tid=" << tid;
  }
  return inserted;
}

thread_state* profiler::get_thread_state() {
  return _thread_states.find(gettid());
}

void profiler::remove_thread() {
  _thread_states.remove(gettid());
  _num_threads_running -= 1;
}

/**
 * Entry point for wrapped threads
 */
void* profiler::start_thread(void* p) {
  thread_start_arg* arg = reinterpret_cast<thread_start_arg*>(p);

  thread_state* state = get_instance().add_thread();
  REQUIRE(state) << "Failed to add thread";

  state->local_delay.store(arg->_parent_delay_time);

  // Make local copies of the function and argument before freeing the arg wrapper
  thread_fn_t real_fn = arg->_fn;
  void* real_arg = arg->_arg;
  delete arg;

  // Start the sampler for this thread
  profiler::get_instance().begin_sampling(state);

  // Run the real thread function
  void* result = real_fn(real_arg);

  // Always exit via pthread_exit
  pthread_exit(result);
}

void profiler::begin_sampling(thread_state* state) {
#ifndef __APPLE__
  // Set the perf_event sampler configuration (Linux)
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(pe));
  pe.type = PERF_TYPE_SOFTWARE;
  pe.config = PERF_COUNT_SW_TASK_CLOCK;
  pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_CALLCHAIN;
  pe.sample_period = SamplePeriod;
  pe.wakeup_events = SampleBatchSize; // This is ignored on linux 3.13 (why?)
  pe.exclude_idle = 1;
  pe.exclude_kernel = 1;
  pe.disabled = 1;

  // Create this thread's perf_event sampler and start sampling
  state->sampler = perf_event(pe);
  state->process_timer = timer(SampleSignal);
  state->process_timer.start_interval(SamplePeriod * SampleBatchSize);
  state->sampler.start();
#else
  // macOS version using timer-based sampling
  state->sampler = perf_event(SamplePeriod);
  state->process_timer = timer(SampleSignal);
  state->process_timer.start_interval(SamplePeriod * SampleBatchSize);
  state->sampler.start();
#endif
}

void profiler::end_sampling() {
  thread_state* state = get_thread_state();
  if(state) {
    state->set_in_use(true);

    process_samples(state);

    state->sampler.stop();
    state->sampler.close();

    remove_thread();
  }
}

std::pair<line*,bool> profiler::match_line(perf_event::record& sample) {
  // bool -> true: hit selected_line
  std::pair<line*, bool> match_res(nullptr, false);
  // flag use to increase the sample only for the first line in the source scope. could it be last line in callchain?
  bool first_hit = false;
  if(!sample.is_sample())
    return match_res;
  // Check if the sample occurred in known code
  line* l = memory_map::get_instance().find_line(sample.get_ip()).get();
  if(l){
    match_res.first = l;
    first_hit = true;
    if(_selected_line == l){
      match_res.second = true;
      return match_res;
    }
  }
  // Walk the callchain
  for(uint64_t pc : sample.get_callchain()) {
    // Need to subtract one. PC is the return address, but we're looking for the callsite.
    l = memory_map::get_instance().find_line(pc-1).get();
    if(l){
      if(!first_hit){
        first_hit = true;
        match_res.first = l;
      }
      if(_selected_line == l){
        match_res.first = l;
	match_res.second = true;
        return match_res;
      }
    }
  }

  // No hits. Return null
  return match_res;
}

void profiler::add_delays(thread_state* state) {
  // Add delays if there is an experiment running
  if(_experiment_active.load()) {
    // Don't execute delays on threads that are blocked (e.g., in pthread_join).
    // On macOS, wall-clock timers fire even on blocked threads, but these threads
    // should not pause — post_block() will handle delay accounting on wake-up.
    if(state->is_blocked.load()) return;

    // Take a snapshot of the global and local delays
    size_t global_delay = _global_delay.load();
    size_t local = state->local_delay.load();

#ifdef __APPLE__
    g_delay_checks.fetch_add(1, std::memory_order_relaxed);
#endif

    // Is this thread ahead or behind on delays?
    if(local > global_delay) {
#ifdef __APPLE__
      // On macOS, process_all_samples() pushes _global_delay when crediting
      // the sampled thread. Don't push it again here — overshoot from nanosleep
      // would amplify _global_delay beyond elapsed time, causing underflow in
      // the duration calculation.
      g_delays_skipped.fetch_add(1, std::memory_order_relaxed);
#else
      // Thread is ahead: increase the global delay time to make other threads pause
      _global_delay.fetch_add(local - global_delay);
#endif

    } else if(local < global_delay) {
      // Thread is behind: Pause this thread to catch up

      // Pause and record the exact amount of time this thread paused
      size_t needed = global_delay - local;
      state->sampler.stop();
      size_t waited = wait(needed);
#ifdef __APPLE__
      // Cap the increment to prevent nanosleep overshoot from inflating delays.
      // macOS nanosleep has ~1ms granularity, and overshoot would propagate
      // through the local > global branch, amplifying _global_delay.
      state->local_delay.fetch_add(needed);
      // Track overshoot separately for duration correction
      if(waited > needed) {
        g_experiment_overshoot.fetch_add(waited - needed, std::memory_order_relaxed);
      }
      g_delays_applied.fetch_add(1, std::memory_order_relaxed);
#else
      state->local_delay.fetch_add(waited);
#endif
      state->sampler.start();
    }

  } else {
    // Just skip ahead on delays if there isn't an experiment running
    state->local_delay.store(_global_delay.load());
  }
}

void profiler::process_samples(thread_state* state) {
  for(perf_event::record r : state->sampler) {
    if(r.is_sample()) {
      // Find and match the line that contains this sample
      std::pair<line*, bool> sampled_line = match_line(r);
      if(sampled_line.first) {
        sampled_line.first->add_sample();
      }

      if(_experiment_active) {
        // Add a delay if the sample is in the selected line
        if(sampled_line.second)
          state->local_delay.fetch_add(_delay_size.load());

      } else if(sampled_line.first != nullptr && _next_line.load() == nullptr
                && !is_coz_header(sampled_line.first)) {
        _next_line.store(sampled_line.first);
      }
    }
  }

  add_delays(state);
}

/**
 * Process samples from all thread states.
 * This is called from the profiler thread on macOS where signal-based
 * sample processing may not work reliably when threads are blocked.
 *
 * The approach: when samples hit the selected line, update the sampled thread's
 * local_delay and push global_delay, then signal all threads to check their delays.
 * Threads that are behind (local < global) will pause themselves in add_delays().
 */
void profiler::process_all_samples() {
  size_t delay_size = _delay_size.load();
  bool experiment_active = _experiment_active.load();
  bool needs_signal = false;
  size_t samples_processed = 0;
  size_t selected_line_hits = 0;

  // Track which thread IDs were sampled on the selected line
  // These threads should NOT be delayed (they're the ones being "sped up")
  std::vector<uint64_t> sampled_tids;

  _thread_states.for_each([this, delay_size, experiment_active, &needs_signal, &samples_processed, &selected_line_hits, &sampled_tids](pid_t tid, thread_state* state) {
    for(perf_event::record r : state->sampler) {
      if(r.is_sample()) {
        samples_processed++;
        std::pair<line*, bool> sampled_line = match_line(r);
        if(sampled_line.first) {
          sampled_line.first->add_sample();
        }

        if(experiment_active && sampled_line.second) {
          selected_line_hits++;
          // Track the sampled thread so we don't delay it
          uint64_t sample_tid = r.get_tid();
          sampled_tids.push_back(sample_tid);

          // Find the thread that was sampled and credit it with the delay
          thread_state* sampled_state = _thread_states.find(static_cast<pid_t>(sample_tid));
          if(sampled_state) {
            // Push global_delay by exactly delay_size so other threads will catch up.
            // Using fetch_add (not CAS-to-new_local) prevents stale local_delay
            // residue from prior experiments inflating _global_delay.
            size_t new_global = _global_delay.fetch_add(delay_size) + delay_size;

            // Ensure the sampled thread's local_delay is at least new_global so it
            // won't be incorrectly delayed in add_delays() — this thread is being
            // "sped up" and should never pause.
            size_t local = sampled_state->local_delay.load();
            while(local < new_global) {
              if(sampled_state->local_delay.compare_exchange_weak(local, new_global)) {
                break;
              }
            }
            needs_signal = true;
          }
        } else if(!experiment_active && sampled_line.first != nullptr && _next_line.load() == nullptr
                  && !is_coz_header(sampled_line.first)) {
          // When not in an experiment, select this line for the next experiment
          _next_line.store(sampled_line.first);
        }
      }
    }
  });

  // Log sample processing stats periodically when verbose (not every 10ms call)
  static size_t pas_log_counter = 0;
  if(samples_processed > 0 && ++pas_log_counter % 100 == 1) {
    VERBOSE << "process_all_samples: processed=" << samples_processed
            << ", selected_hits=" << selected_line_hits
            << ", delay_size=" << delay_size;
  }

  // Delays are applied via the counter-based mechanism: process_all_samples()
  // credits the sampled thread's local_delay and pushes global_delay; other
  // threads pause themselves in add_delays() when they see local < global.
  // This matches the Linux implementation and ensures delay time is properly
  // tracked in global_delay for the duration correction.
}

/**
 * Apply pending delays to all threads by signaling them to self-regulate.
 * This is used on macOS where per-thread timers are not available.
 * Threads will check their delay debt in the signal handler and pause themselves.
 */
void profiler::apply_pending_delays() {
#ifdef __APPLE__
  // Don't signal threads during shutdown — they may have already exited
  if(!_running.load()) return;

  // Signal all threads to wake up and check their delays.
  // Each thread will call add_delays() in its signal handler,
  // which will cause it to pause if it's behind on delays.
  _thread_states.for_each([](pid_t tid, thread_state* state) {
    // Check _active first — stop() zeroes _target_pthread and then sets _active=false,
    // so if _active is false the thread is shutting down and we must not signal it.
    if(!state->sampler._active.load(std::memory_order_acquire)) return;
    pthread_t target = state->sampler._target_pthread;
    if(target != 0) {
      pthread_kill(target, SampleSignal);
    }
  });
#endif
}

/**
 * Entry point for the profiler thread
 */
void* profiler::start_profiler_thread(void* arg) {
  spinlock* l = (spinlock*)arg;
  profiler::get_instance().profiler_thread(*l);
  // Return instead of calling pthread_exit. On macOS, real::pthread_exit goes
  // through DYLD interposition and would trigger handle_pthread_exit recursion.
  return nullptr;
}

void profiler::samples_ready(int signum, siginfo_t* info, void* p) {
  thread_state* state = get_instance().get_thread_state();
  if (!state) {
    return;
  }
  if (state->check_in_use()) {
    return;
  }

#ifdef __APPLE__
  // On macOS, samples are processed centrally by process_all_samples() on the
  // profiler thread. The signal handler only needs to apply pending delays.
  // Processing samples here would double-credit delays because all samples
  // are stored in the main thread's sampler regardless of which thread was sampled.
  state->set_in_use(true);
  get_instance().add_delays(state);
  state->set_in_use(false);
#else
  // On Linux, each thread has its own perf_event with samples.
  // Process this thread's samples and apply delays.
  profiler::get_instance().process_samples(state);
#endif
}

void profiler::on_error(int signum, siginfo_t* info, void* p) {
  if(signum == SIGSEGV) {
    fprintf(stderr, "Segmentation fault at %p\n", info->si_addr);
  } else if(signum == SIGABRT) {
    fprintf(stderr, "Aborted!\n");
  } else if(signum == SIGBUS) {
    fprintf(stderr, "Bus error at %p\n", info->si_addr);
  } else {
    fprintf(stderr, "Signal %d at %p\n", signum, info->si_addr);
  }

  void* buf[256];
  int frames = backtrace(buf, 256);
  char** syms = backtrace_symbols(buf, frames);

  for(int i=0; i<frames; i++) {
    fprintf(stderr, "  %d: %s\n", i, syms[i]);
  }

  _exit(2);
}
