#if !defined(CAUSAL_RUNTIME_SIGLOCK_H)
#define CAUSAL_RUNTIME_SIGLOCK_H

#include <atomic>

/**
 * A lock designed to protect data shared between a thread and asynchronous signal handling code
 * that is *running in the same thread*. Code that acquires the lock must specify its context,
 * either `thread` or `signal`. Lock acquisition has several cases:
 *  1. The lock is available: either context can acquire the lock immediately.
 *  2. The lock is held in thread context: lock acquisition will return failure immediately
 *       If acquiring in thread context, the lock is being used recursively (unsupported). If
 *       acquiring in signal context, the signal handler must have preempted the thread while
 *       holding the lock. In either case, blocking would result in deadlock.
 *  3. The lock is held in signal context: depends
 *       If acquiring in thread context, spin on the lock.
 *       If acquiring in signal context, return failure.
 */
class siglock {
public:
  enum context {
    signal_context = 1,
    thread_context = 2
  };
  
  siglock() = default;
  
  /// Attempt to acquire the lock in a given context
  inline bool lock(context c) {
    if(c == thread_context) {
      _current = thread_context;
      std::atomic_signal_fence(std::memory_order_acq_rel);
      return true;
    } else {
      return _current == 0;
    }
  }
  
  /// Release the lock
  inline void unlock() {
    _current = 0;
    std::atomic_signal_fence(std::memory_order_acq_rel);
  }
  
private:
  int _current = 0;
};

#endif
