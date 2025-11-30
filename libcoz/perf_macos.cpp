/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * Copyright (c) 2025, macOS port additions
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifdef __APPLE__

#include "perf_macos.h"

#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/sysctl.h>
#include <unistd.h>

extern "C" int kperf_lightweight_pet_set(uint32_t enabled) {
    size_t size = sizeof(enabled);
    // This controls the "lightweight PET" mode via sysctl.
    // Returns 0 on success, or -1 / errno on failure.
    return sysctlbyname("kperf.lightweight_pet",
                        nullptr, nullptr,
                        &enabled, size);
}

#include "util.h"
#include "ccutil/log.h"
#include "ccutil/wrapped_array.h"

using ccutil::wrapped_array;

// Global state for kperf initialization
static bool g_kperf_initialized = false;
static pthread_mutex_t g_kperf_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize kperf for sampling
static void init_kperf() {
  pthread_mutex_lock(&g_kperf_mutex);

  if (!g_kperf_initialized) {
    // Set up kperf for lightweight PET (Profile Every Thread) mode
    // This is the safest mode that works without kernel debugging enabled

    // Configure action 1 to sample user stacks and thread info
    uint32_t samplers = KPERF_SAMPLER_USTACK | KPERF_SAMPLER_TINFO;
    int ret = kperf_action_samplers_set(1, samplers);
    if (ret != 0) {
      WARNING << "kperf_action_samplers_set failed: " << ret
	      << ". Sampling may not work correctly. "
	      << "Try running with sudo or adjusting System Integrity Protection.";
    }

    // Set action count to 1
    kperf_action_count_set(1);

    // Enable lightweight PET mode
    kperf_lightweight_pet_set(1);

    g_kperf_initialized = true;
  }

  pthread_mutex_unlock(&g_kperf_mutex);
}

// Default constructor
perf_event::perf_event() {}

// Create a timer-based sampling event
perf_event::perf_event(uint64_t sample_period_ns, pid_t pid) :
    _sample_period_ns(sample_period_ns), _pid(pid) {

  init_kperf();

  // Set up sample type (we support IP, thread info, and callchain)
  _sample_type = static_cast<uint64_t>(sample::ip) |
                 static_cast<uint64_t>(sample::pid_tid) |
                 static_cast<uint64_t>(sample::time) |
                 static_cast<uint64_t>(sample::callchain) |
                 static_cast<uint64_t>(sample::cpu);

  // Store current thread for signaling
  _signal_thread = pthread_self();

  // Create dispatch queue for timer
  _queue = dispatch_queue_create("com.coz.sampling", DISPATCH_QUEUE_SERIAL);
}

// Move constructor
perf_event::perf_event(perf_event&& other) {
  _active = other._active;
  _sample_count = other._sample_count;
  _sample_type = other._sample_type;
  _sample_period_ns = other._sample_period_ns;
  _timer = other._timer;
  _queue = other._queue;
  _ready_signal = other._ready_signal;
  _pid = other._pid;
  _signal_thread = other._signal_thread;

  other._timer = nullptr;
  other._queue = nullptr;
  other._active = false;
}

// Destructor
perf_event::~perf_event() {
  close();
}

// Move assignment
void perf_event::operator=(perf_event&& other) {
  if (this != &other) {
    close();

    _active = other._active;
    _sample_count = other._sample_count;
    _sample_type = other._sample_type;
    _sample_period_ns = other._sample_period_ns;
    _timer = other._timer;
    _queue = other._queue;
    _ready_signal = other._ready_signal;
    _pid = other._pid;
    _signal_thread = other._signal_thread;

    other._timer = nullptr;
    other._queue = nullptr;
    other._active = false;
  }
}

// Get sample count
uint64_t perf_event::get_count() const {
  return _sample_count;
}

// Start sampling
void perf_event::start() {
  if (_active) return;

  _active = true;
  _sample_count = 0;

  // Start kperf sampling
  int ret = kperf_sample_on();
  if (ret != 0) {
    WARNING << "kperf_sample_on failed: " << ret;
  }

  // Create a timer that fires periodically
  if (_queue) {
    _timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _queue);

    if (_timer) {
      // Convert nanoseconds to dispatch time
      uint64_t interval_ns = _sample_period_ns;
      dispatch_time_t start = dispatch_time(DISPATCH_TIME_NOW, interval_ns);

      dispatch_source_set_timer(_timer, start, interval_ns, interval_ns / 10);

      // Capture necessary state for the timer handler
      int signal = _ready_signal;
      pthread_t thread = _signal_thread;
      uint64_t* count_ptr = &_sample_count;

      dispatch_source_set_event_handler(_timer, ^{
        // Trigger a sample
        kperf_sample_set(1);

        // Increment sample count
        __atomic_add_fetch(count_ptr, 1, __ATOMIC_RELAXED);

        // Signal the profiler thread if configured
        if (signal != 0) {
          pthread_kill(thread, signal);
        }
      });

      dispatch_resume(_timer);
    }
  }
}

// Stop sampling
void perf_event::stop() {
  if (!_active) return;

  _active = false;

  // Stop kperf sampling
  kperf_sample_off();

  // Cancel and release timer
  if (_timer) {
    dispatch_source_cancel(_timer);
    dispatch_release(_timer);
    _timer = nullptr;
  }
}

// Close the event
void perf_event::close() {
  stop();

  if (_queue) {
    dispatch_release(_queue);
    _queue = nullptr;
  }
}

// Configure signal delivery
void perf_event::set_ready_signal(int sig) {
  _ready_signal = sig;
  _signal_thread = pthread_self();
}

// Record methods
uint64_t perf_event::record::get_ip() const {
  return _ip;
}

uint64_t perf_event::record::get_pid() const {
  return _pid;
}

uint64_t perf_event::record::get_tid() const {
  return _tid;
}

uint64_t perf_event::record::get_time() const {
  return _time;
}

uint32_t perf_event::record::get_cpu() const {
  return _cpu;
}

wrapped_array<uint64_t> perf_event::record::get_callchain() const {
  if (_callchain.empty()) {
    return wrapped_array<uint64_t>(nullptr, 0);
  }
  return wrapped_array<uint64_t>(
      const_cast<uint64_t*>(_callchain.data()),
      _callchain.size());
}

// Iterator methods
perf_event::record perf_event::iterator::get() {
  record r(record_type::sample);

  // On macOS, we don't have a ring buffer like Linux perf
  // We use kperf's lightweight sampling which doesn't provide
  // individual sample records to userspace
  // Instead, we return synthetic records based on the current thread state

  // Get current thread
  thread_t thread = mach_thread_self();

  // Get thread info
  thread_identifier_info_data_t info;
  mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
  kern_return_t kr = thread_info(thread, THREAD_IDENTIFIER_INFO,
                                   (thread_info_t)&info, &count);

  if (kr == KERN_SUCCESS) {
    r._tid = info.thread_id;
    r._pid = getpid();
  }

  // Get timestamp
  r._time = mach_absolute_time();

  // Note: We can't easily get IP and callchain without kdebug trace buffer
  // For now, return empty values
  r._ip = 0;

  mach_port_deallocate(mach_task_self(), thread);

  return r;
}

#endif // __APPLE__
