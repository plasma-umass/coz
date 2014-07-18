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
  
  siglock() {
    REQUIRE(_lock.is_lock_free()) << "Siglock is not lock free!";
  }
  
  /// Attempt to acquire the lock in a given context
  inline bool lock(context c) {
    // Attempt to acquire the lock. If locking succeeds, use acquire ordering
    int expected = 0;
    while(!_lock.compare_exchange_strong(expected, c, 
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
      // Return failure if acquisition failed in signal context, or if the lock is thread-owned
      if(true /*c == signal_context || expected == thread_context*/) {
        return false;
      }
      // Reset `expected` to try acquisition again
      expected = 0;
      // Relax.
      __asm__("pause");
    }
    // Success!
    return true;
  }
  
  /// Release the lock
  inline void unlock() {
    // Use release memory ordering to ensure earlier writes are ordered before the lock release
    _lock.store(0, std::memory_order_release);
  }
  
private:
  std::atomic<int> _lock = ATOMIC_VAR_INIT(0);
};

#endif
