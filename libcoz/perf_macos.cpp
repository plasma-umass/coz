/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * Copyright (c) 2025, macOS port additions
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifdef __APPLE__

#include "perf_macos.h"

#include <cerrno>
#include <cstring>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/thread_info.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "ccutil/spinlock.h"

#include "ccutil/log.h"
#include "ccutil/wrapped_array.h"

// Use the originals from mac_interpose.cpp to avoid coz DYLD interposition
extern "C" int coz_orig_pthread_create(pthread_t*, const pthread_attr_t*,
                                        void*(*)(void*), void*);
extern "C" int coz_orig_pthread_sigmask(int, const sigset_t*, sigset_t*);

using ccutil::wrapped_array;

// Get thread ID from Mach port
static inline uint64_t get_thread_id_from_port(mach_port_t thread_port) {
  thread_identifier_info_data_t info;
  mach_msg_type_number_t info_count = THREAD_IDENTIFIER_INFO_COUNT;
  kern_return_t kr = thread_info(thread_port, THREAD_IDENTIFIER_INFO,
                                  (thread_info_t)&info, &info_count);
  if (kr == KERN_SUCCESS) {
    return info.thread_id;
  }
  return 0;
}

// Get PC from a suspended thread using Mach APIs
static inline uint64_t get_pc_from_thread(mach_port_t thread_port) {
#if defined(__arm64__) || defined(__aarch64__)
  arm_thread_state64_t state;
  mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
  kern_return_t kr = thread_get_state(thread_port, ARM_THREAD_STATE64,
                                       (thread_state_t)&state, &count);
  if (kr == KERN_SUCCESS) {
    // On ARM64, use arm_thread_state64_get_pc for PAC compatibility
    return arm_thread_state64_get_pc(state);
  }
#elif defined(__x86_64__)
  x86_thread_state64_t state;
  mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
  kern_return_t kr = thread_get_state(thread_port, x86_THREAD_STATE64,
                                       (thread_state_t)&state, &count);
  if (kr == KERN_SUCCESS) {
    return state.__rip;
  }
#endif
  return 0;
}

// Global sampling state - use function-local statics to avoid initialization order issues
static spinlock& get_events_lock() {
  static spinlock lock;
  return lock;
}

static std::vector<perf_event*>& get_registered_events() {
  static std::vector<perf_event*> events;
  return events;
}
static pthread_t g_sampling_thread;
static std::atomic<bool> g_sampling_active{false};
static mach_port_t g_sampling_thread_port = MACH_PORT_NULL;
static uint64_t g_sample_period_ns = 1000000; // 1ms default

// Ports of coz-internal threads that the sampling thread must never suspend.
static spinlock& get_internal_lock() {
  static spinlock lock;
  return lock;
}
static std::vector<mach_port_t>& get_internal_ports() {
  static std::vector<mach_port_t> ports;
  return ports;
}

static bool is_internal_thread(mach_port_t port) {
  if (port == g_sampling_thread_port) return true;
  spinlock& lock = get_internal_lock();
  std::vector<mach_port_t>& ports = get_internal_ports();
  lock.lock();
  bool found = std::find(ports.begin(), ports.end(), port) != ports.end();
  lock.unlock();
  return found;
}


// Store a sample for a given event and signal the target thread
static void store_sample(perf_event* event, uint64_t ip, uint64_t tid, pthread_t target_pthread) {
  if (!event->_active.load(std::memory_order_relaxed)) return;
  if (ip == 0) return;

  // Write sample to ring buffer
  size_t write_idx = event->_write_index.load(std::memory_order_relaxed);
  size_t next_idx = (write_idx + 1) & SAMPLE_BUFFER_MASK;

  // Store sample
  event->_samples[write_idx].ip = ip;
  event->_samples[write_idx].time = mach_absolute_time();
  event->_samples[write_idx].tid = tid;

  // Update write index
  event->_write_index.store(next_idx, std::memory_order_release);

  // Increment sample count
  event->_sample_count.fetch_add(1, std::memory_order_relaxed);

  // Signal the target thread to process samples (if signal is configured)
  int sig = event->get_ready_signal();
  if (sig != 0 && target_pthread != 0) {
    pthread_kill(target_pthread, sig);
  }
}

// Sample ALL threads in the process (not just registered ones)
// This is needed because pthread interposition may not work on macOS
static void sample_all_threads() {
  spinlock& lock = get_events_lock();
  std::vector<perf_event*>& events = get_registered_events();

  // Get all threads in the current task
  thread_act_array_t threads;
  mach_msg_type_number_t thread_count;
  kern_return_t kr = task_threads(mach_task_self(), &threads, &thread_count);
  if (kr != KERN_SUCCESS) {
    return;
  }

  // Find the main thread's event (first registered one that's active)
  lock.lock();
  perf_event* main_event = nullptr;
  pthread_t main_pthread = 0;
  for (perf_event* event : events) {
    if (event->_active.load(std::memory_order_relaxed) &&
        event->_target_thread != g_sampling_thread_port) {
      main_event = event;
      main_pthread = event->_target_pthread;
      break;
    }
  }
  lock.unlock();

  // Sample each thread
  for (mach_msg_type_number_t i = 0; i < thread_count; i++) {
    mach_port_t thread = threads[i];

    // Skip all coz-internal threads (sampling thread, profiler thread)
    if (is_internal_thread(thread)) continue;

    // Suspend the target thread
    kr = thread_suspend(thread);
    if (kr != KERN_SUCCESS) {
      continue;
    }

    // Get the PC
    uint64_t ip = get_pc_from_thread(thread);
    uint64_t tid = get_thread_id_from_port(thread);

    // Resume immediately
    thread_resume(thread);

    // Store the sample using the main event (samples from all threads go here)
    if (ip != 0 && main_event != nullptr) {
      store_sample(main_event, ip, tid, main_pthread);
    }
  }

  // Deallocate the thread list
  for (mach_msg_type_number_t i = 0; i < thread_count; i++) {
    mach_port_deallocate(mach_task_self(), threads[i]);
  }
  vm_deallocate(mach_task_self(), (vm_address_t)threads, thread_count * sizeof(thread_act_t));
}

// Sampling thread function
static void* sampling_thread_func(void* arg) {
  // Get our own Mach thread port
  g_sampling_thread_port = mach_thread_self();

  // Block SIGPROF in sampling thread to avoid self-interference.
  // Use the original pthread_sigmask to bypass our DYLD interposition
  // which strips coz's signals from SIG_BLOCK masks.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGPROF);
  coz_orig_pthread_sigmask(SIG_BLOCK, &mask, nullptr);

  // Calculate sleep interval
  struct timespec sleep_time;
  sleep_time.tv_sec = g_sample_period_ns / 1000000000;
  sleep_time.tv_nsec = g_sample_period_ns % 1000000000;

  while (g_sampling_active.load(std::memory_order_relaxed)) {
    sample_all_threads();
    nanosleep(&sleep_time, nullptr);
  }

  return nullptr;
}

// Start the global sampling thread
void macos_sampling_start() {
  if (g_sampling_active.load(std::memory_order_relaxed)) {
    return;
  }

  g_sampling_active.store(true, std::memory_order_release);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  int rc = coz_orig_pthread_create(&g_sampling_thread, &attr, sampling_thread_func, nullptr);
  if (rc != 0) {
    g_sampling_active.store(false, std::memory_order_release);
  }

  pthread_attr_destroy(&attr);
}

// Stop the global sampling thread
void macos_sampling_stop() {
  if (!g_sampling_active.load(std::memory_order_relaxed)) return;

  g_sampling_active.store(false, std::memory_order_release);
  pthread_join(g_sampling_thread, nullptr);
}

// Register an event for sampling
void macos_sampling_register_event(perf_event* event) {
  spinlock& lock = get_events_lock();
  std::vector<perf_event*>& events = get_registered_events();

  lock.lock();
  events.push_back(event);
  size_t count = events.size();
  lock.unlock();

  // Start sampling thread if this is the first event
  if (count == 1) {
    macos_sampling_start();
  }
}

// Unregister an event
void macos_sampling_unregister_event(perf_event* event) {
  spinlock& lock = get_events_lock();
  std::vector<perf_event*>& events = get_registered_events();

  lock.lock();
  auto it = std::find(events.begin(), events.end(), event);
  if (it != events.end()) {
    events.erase(it);
  }
  lock.unlock();
}

// Default constructor
perf_event::perf_event() : _active(false), _sample_count(0), _target_thread(MACH_PORT_NULL), _target_pthread(0) {}

// Create a sampling event for current thread
perf_event::perf_event(uint64_t sample_period_ns, pid_t pid) :
    _sample_period_ns(sample_period_ns), _pid(pid), _active(false), _sample_count(0) {

  // Get the Mach port and pthread_t for the current thread
  _target_thread = mach_thread_self();
  _target_pthread = pthread_self();

  // Set up sample type
  _sample_type = static_cast<uint64_t>(sample::ip) |
                 static_cast<uint64_t>(sample::pid_tid) |
                 static_cast<uint64_t>(sample::time);

  // Set global sample period
  g_sample_period_ns = sample_period_ns;
}

// Move constructor
perf_event::perf_event(perf_event&& other) :
    _sample_count(other._sample_count.load()),
    _sample_type(other._sample_type),
    _sample_period_ns(other._sample_period_ns),
    _ready_signal(other._ready_signal),
    _pid(other._pid),
    _target_thread(other._target_thread),
    _target_pthread(other._target_pthread),
    _write_index(other._write_index.load()),
    _read_index(other._read_index) {

  _active.store(other._active.load(std::memory_order_relaxed), std::memory_order_relaxed);

  // Copy samples
  for (size_t i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
    _samples[i] = other._samples[i];
  }

  other._active.store(false, std::memory_order_relaxed);
  other._sample_count = 0;
  other._target_thread = MACH_PORT_NULL;
  other._target_pthread = 0;
}

// Destructor
perf_event::~perf_event() {
  close();
}

// Move assignment
void perf_event::operator=(perf_event&& other) {
  if (this != &other) {
    close();

    _active.store(other._active.load(std::memory_order_relaxed), std::memory_order_relaxed);
    _sample_count = other._sample_count.load();
    _sample_type = other._sample_type;
    _sample_period_ns = other._sample_period_ns;
    _ready_signal = other._ready_signal;
    _pid = other._pid;
    _target_thread = other._target_thread;
    _target_pthread = other._target_pthread;
    _write_index = other._write_index.load();
    _read_index = other._read_index;

    for (size_t i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
      _samples[i] = other._samples[i];
    }

    other._active.store(false, std::memory_order_relaxed);
    other._sample_count = 0;
    other._target_thread = MACH_PORT_NULL;
    other._target_pthread = 0;
  }
}

// Get sample count
uint64_t perf_event::get_count() const {
  return _sample_count.load(std::memory_order_relaxed);
}

// Start sampling
void perf_event::start() {
  if (_active.load(std::memory_order_relaxed)) return;

  _active.store(true, std::memory_order_release);
  macos_sampling_register_event(this);
}

// Stop sampling
void perf_event::stop() {
  if (!_active.load(std::memory_order_relaxed)) return;

  // Zero _target_pthread before unregistering so that apply_pending_delays()
  // won't call pthread_kill() on a stale handle after the thread exits.
  _target_pthread = 0;

  _active.store(false, std::memory_order_release);
  macos_sampling_unregister_event(this);
}

// Close the event
void perf_event::close() {
  stop();
}

// Configure signal delivery
void perf_event::set_ready_signal(int sig) {
  _ready_signal = sig;
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
  // No callchain support in thread suspension sampling
  return wrapped_array<uint64_t>(nullptr, 0);
}

// Iterator constructor
perf_event::iterator::iterator(perf_event& source, bool at_end) :
    _source(source), _at_end(at_end) {
  _read_index = source._read_index;

  // Check if there's any data
  if (!at_end) {
    size_t write_idx = source._write_index.load(std::memory_order_acquire);
    if (_read_index == write_idx) {
      _at_end = true;
    }
  }
}

void perf_event::iterator::next() {
  if (_at_end) return;

  _read_index = (_read_index + 1) & SAMPLE_BUFFER_MASK;
  _source._read_index = _read_index;

  // Check if we've caught up to the writer
  size_t write_idx = _source._write_index.load(std::memory_order_acquire);
  if (_read_index == write_idx) {
    _at_end = true;
  }
}

perf_event::record perf_event::iterator::get() {
  record r(record_type::sample);

  if (!_at_end) {
    const sample_entry& s = _source._samples[_read_index];
    r._ip = s.ip;
    r._time = s.time;
    r._tid = s.tid;
    r._pid = getpid();
  }

  return r;
}

perf_event::iterator perf_event::begin() {
  return iterator(*this, false);
}

perf_event::iterator perf_event::end() {
  return iterator(*this, true);
}

mach_port_t macos_get_sampling_thread_port() {
  return g_sampling_thread_port;
}

void macos_register_internal_thread(mach_port_t port) {
  spinlock& lock = get_internal_lock();
  std::vector<mach_port_t>& ports = get_internal_ports();
  lock.lock();
  ports.push_back(port);
  lock.unlock();
}

#endif // __APPLE__
