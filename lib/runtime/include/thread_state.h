#if !defined(CAUSAL_RUNTIME_THREAD_STATE_H)
#define CAUSAL_RUNTIME_THREAD_STATE_H

#include "siglock.h"
#include "timer.h"

class thread_state {
public:
  /// The count of delays (or selected line visits) in the thread
  size_t delay_count = 0;
  /// Any excess delay time added when nanosleep() returns late
  size_t excess_delay = 0;
  /// A snapshot of the global delay count, taken before blocking on a pthread_* function
  size_t snapshot = 0;
  /// The sampler object for this thread
  perf_event sampler;
  /// The timer that triggers sample processing for this thread
  timer process_timer;
  
  class ref {
  public:
    ref(thread_state* s, siglock::context c, bool force = false) {
      if(s->_l.lock(c) || force) {
        _s = s;
      } else {
        _s = nullptr;
      }
    }
    
    ref(ref&& other) {
      _s = other._s;
      other._s = nullptr;
    }
    
    ~ref() {
      if(_s != nullptr) {
        _s->_l.unlock();
      }
    }
    
    inline void operator=(ref&& other) {
      _s = other._s;
      other._s = nullptr;
    }
    
    inline operator bool() {
      return _s != nullptr;
    }
    
    inline thread_state* operator->() {
      return _s;
    }
    
  private:
    ref(const ref&) = delete;
    void operator=(const ref&) = delete;
    
    thread_state* _s;
  };
  
  /**
  * Get the thread local state in a reference. The `force` parameter should only be
  * true when in error-handling mode. This makes it possible to collect a backtrace, which
  * calls pthread_mutex_lock.
  */
  static ref get(siglock::context c, bool force = false) {
    static thread_local thread_state s;
    return thread_state::ref(&s, c, force);
  }
  
private:
  friend class ref;
  
  /// A siglock used to ensure access to thread state is atomic
  siglock _l;
};

#endif
