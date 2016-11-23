#if !defined(CCUTIL_SPINLOCK_H)
#define CCUTIL_SPINLOCK_H

#include <atomic>

class spinlock {
public:
  inline void lock() {
    while(_flag.test_and_set()) {
#if defined(__i386__) || defined(__x86_64__)
      /*
       * NOTE: "rep nop" works on all Intel architectures and has the same
       * encoding as "pause" on the newer ones.
       */
      __asm__ __volatile__ ("rep nop");
#else
      /* nothing */
#endif
    }
  }
  
  inline bool trylock() {
    return !_flag.test_and_set();
  }
  
  inline void unlock() {
    _flag.clear();
  }
  
private:
  std::atomic_flag _flag = ATOMIC_FLAG_INIT;
};

#endif
