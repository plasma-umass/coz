#if !defined(CAUSAL_RUNTIME_THREAD_STATE_H)
#define CAUSAL_RUNTIME_THREAD_STATE_H

#include <atomic>

#include "siglock.h"
#include "timer.h"

class thread_state {
public:
  bool in_use = false;      //< Set by the main thread to prevent signal handler from racing
  size_t delay_count = 0;   //< The count of delays (or selected line visits) in the thread
  size_t excess_delay = 0;  //< Any excess delay time added when nanosleep() returns late
  perf_event sampler;       //< The sampler object for this thread
  timer process_timer;      //< The timer that triggers sample processing for this thread
  size_t pre_block_time;    //< The time saved before (possibly) blocking
  
  inline void set_in_use(bool value) {
    in_use = value;
    std::atomic_signal_fence(std::memory_order_seq_cst);
  }
  
  bool check_in_use() {
    return in_use;
  }
    
  
  class ref {
  public:
    ref(thread_state* s, siglock::context c) {
      if(s->_l.lock(c)) {
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
  static thread_state& get() {
    static thread_local thread_state s;
    return s;
    //return thread_state::ref(&s, c);
  }
  
private:
  friend class ref;
  
  /// A siglock used to ensure access to thread state is atomic
  siglock _l;
};

#endif
