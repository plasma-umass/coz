#ifdef __APPLE__

#include "perf_macos.h"

#include <execinfo.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <array>

#include "util.h"
namespace {
constexpr size_t kMaxFrames = 64;   // Reduced - we rarely need deep stacks
constexpr size_t kTrimFrames = 3;   // drop signal handler frames
}

perf_event::perf_event() {
  _current_sample = record(record_type::sample);
  _current_sample.reserve_callchain(kMaxFrames);
}

perf_event::perf_event(uint64_t sample_period_ns, pid_t pid)
    : _sample_period_ns(sample_period_ns),
      _pid(pid),
      _signal_thread(pthread_self()) {
  _queue = dispatch_queue_create("com.coz.sampling", DISPATCH_QUEUE_SERIAL);
  _current_sample = record(record_type::sample);
  _current_sample.reserve_callchain(kMaxFrames);
}

perf_event::perf_event(perf_event&& other) noexcept {
  *this = std::move(other);
}

perf_event::~perf_event() {
  close();
}

void perf_event::operator=(perf_event&& other) noexcept {
  if(this == &other) return;
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
  _has_sample = other._has_sample;
  _current_sample = other._current_sample;

  other._timer = nullptr;
  other._queue = nullptr;
  other._active = false;
  other._has_sample = false;
}

uint64_t perf_event::get_count() const {
  return _sample_count;
}

void perf_event::start() {
  if(_active)
    return;
  _active = true;
  _sample_count = 0;

  if(!_queue || _ready_signal == 0)
    return;

  _timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _queue);
  if(!_timer)
    return;

  uint64_t interval = std::max<uint64_t>(_sample_period_ns, 1);
  dispatch_time_t start_time = dispatch_time(DISPATCH_TIME_NOW, interval);
  dispatch_source_set_timer(_timer, start_time, interval, interval / 10);
  dispatch_set_context(_timer, this);
  dispatch_source_set_event_handler_f(_timer, &perf_event::timer_callback);
  dispatch_resume(_timer);
}

void perf_event::stop() {
  if(!_active)
    return;
  _active = false;

  if(_timer) {
    dispatch_source_cancel(_timer);
    dispatch_release(_timer);
    _timer = nullptr;
  }
}

void perf_event::close() {
  stop();
  if(_queue) {
    dispatch_release(_queue);
    _queue = nullptr;
  }
  _has_sample = false;
}

void perf_event::set_ready_signal(int sig) {
  _ready_signal = sig;
  _signal_thread = pthread_self();
}

void perf_event::capture_sample(void* ucontext) {
  _current_sample.reset(record_type::sample);
  _current_sample._pid = (_pid == 0) ? getpid() : _pid;

  // Extract PC directly from ucontext - this is the most accurate
  // location where the signal interrupted execution
  uint64_t context_pc = 0;
  if(ucontext) {
    auto* ctx = static_cast<ucontext_t*>(ucontext);
#if defined(__arm64__) || defined(__aarch64__)
    // Use macOS-provided macro which handles PAC (pointer authentication)
    context_pc = __darwin_arm_thread_state64_get_pc(ctx->uc_mcontext->__ss);
#elif defined(__x86_64__)
    context_pc = ctx->uc_mcontext->__ss.__rip;
#endif
  }

  // Get backtrace for callchain
  std::array<void*, kMaxFrames> frames{};
  int captured = backtrace(frames.data(), static_cast<int>(frames.size()));
  int start = std::min<int>(captured, static_cast<int>(kTrimFrames));

  // Use context_pc as the primary IP - let match_line() filter via memory_map
  // This avoids expensive dladdr() calls on every sample
  uint64_t ip = context_pc;
  if(ip == 0 && start < captured) {
    ip = reinterpret_cast<uint64_t>(frames[start]);
  }

  _current_sample._ip = ip;

  // Build callchain - include context_pc at the front if we have it
  if(context_pc) {
    _current_sample._callchain.push_back(context_pc);
  }
  for(int i = start; i < captured; i++) {
    _current_sample._callchain.push_back(
        reinterpret_cast<uint64_t>(frames[i]));
  }

  _current_sample._time = mach_absolute_time();
  _current_sample._tid = static_cast<uint64_t>(pthread_mach_thread_np(pthread_self()));
  _has_sample = true;  // Always report sample, let profiler filter via memory_map
}

uint64_t perf_event::record::get_ip() const { return _ip; }
uint64_t perf_event::record::get_pid() const { return _pid; }
uint64_t perf_event::record::get_tid() const { return _tid; }
uint64_t perf_event::record::get_time() const { return _time; }
uint32_t perf_event::record::get_cpu() const { return _cpu; }
ccutil::wrapped_array<uint64_t> perf_event::record::get_callchain() const {
  if(_callchain.empty()) {
    return ccutil::wrapped_array<uint64_t>(nullptr, 0);
  }
  return ccutil::wrapped_array<uint64_t>(
      const_cast<uint64_t*>(_callchain.data()), _callchain.size());
}

perf_event::record perf_event::iterator::get() {
  record r = _source._current_sample;
  _source._has_sample = false;
  _at_end = true;
  return r;
}

perf_event::iterator perf_event::begin() {
  return iterator(*this, !_has_sample);
}

void perf_event::timer_callback(void* context) {
  auto* self = static_cast<perf_event*>(context);
  if(!self || self->_ready_signal == 0)
    return;

  __atomic_add_fetch(&self->_sample_count, 1, __ATOMIC_RELAXED);

  if(self->_signal_thread) {
    pthread_kill(self->_signal_thread, self->_ready_signal);
  }
}

#endif // __APPLE__
