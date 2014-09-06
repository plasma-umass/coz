#include "causal/real.h"

#include <dlfcn.h>

#include "ccutil/log.h"

extern "C" void* __libc_dlsym(void *map, const char* name);
extern "C" void* __libc_dlopen_mode(const char* file, int mode);

#define DEFINE_WRAPPER(name) decltype(::name)* name
#define SET_WRAPPER(name, handle) name = (decltype(::name)*)dlsym(handle, #name)

extern "C" {
  extern int __pthread_mutex_lock(pthread_mutex_t*) __THROW;
  extern int __pthread_mutex_unlock(pthread_mutex_t*) __THROW;
  extern int __pthread_mutex_trylock(pthread_mutex_t*) __THROW;
}

namespace real {
  DEFINE_WRAPPER(main);

  DEFINE_WRAPPER(calloc);

  DEFINE_WRAPPER(exit);
  DEFINE_WRAPPER(_exit);
  DEFINE_WRAPPER(_Exit);
  DEFINE_WRAPPER(fork);

  DEFINE_WRAPPER(sigaction);
  DEFINE_WRAPPER(signal);
  DEFINE_WRAPPER(kill);
  DEFINE_WRAPPER(sigprocmask);
  DEFINE_WRAPPER(sigwait);
  DEFINE_WRAPPER(sigwaitinfo);
  DEFINE_WRAPPER(sigtimedwait);

  DEFINE_WRAPPER(pthread_create);
  DEFINE_WRAPPER(pthread_exit);
  DEFINE_WRAPPER(pthread_join);
  DEFINE_WRAPPER(pthread_sigmask);
  DEFINE_WRAPPER(pthread_kill);
  
  DEFINE_WRAPPER(pthread_mutex_lock) = __pthread_mutex_lock;
  DEFINE_WRAPPER(pthread_mutex_unlock) = __pthread_mutex_unlock;
  DEFINE_WRAPPER(pthread_mutex_trylock) = __pthread_mutex_trylock;
  
  DEFINE_WRAPPER(pthread_cond_wait);
  DEFINE_WRAPPER(pthread_cond_timedwait);
  DEFINE_WRAPPER(pthread_cond_signal);
  DEFINE_WRAPPER(pthread_cond_broadcast);
  
  DEFINE_WRAPPER(pthread_barrier_wait);

  void init() {
    SET_WRAPPER(main, RTLD_NEXT);
    
    SET_WRAPPER(calloc, RTLD_NEXT);

    SET_WRAPPER(exit, RTLD_NEXT);
    SET_WRAPPER(_exit, RTLD_NEXT);
    SET_WRAPPER(_Exit, RTLD_NEXT);
    SET_WRAPPER(fork, RTLD_NEXT);

    SET_WRAPPER(sigaction, RTLD_NEXT);
    SET_WRAPPER(signal, RTLD_NEXT);
    SET_WRAPPER(kill, RTLD_NEXT);
    SET_WRAPPER(sigprocmask, RTLD_NEXT);
    SET_WRAPPER(sigwait, RTLD_NEXT);
    SET_WRAPPER(sigwaitinfo, RTLD_NEXT);
    SET_WRAPPER(sigtimedwait, RTLD_NEXT);
    
    SET_WRAPPER(pthread_create, RTLD_NEXT);
    SET_WRAPPER(pthread_exit, RTLD_NEXT);
    SET_WRAPPER(pthread_join, RTLD_NEXT);
    SET_WRAPPER(pthread_sigmask, RTLD_NEXT);
    SET_WRAPPER(pthread_kill, RTLD_NEXT);
    
    SET_WRAPPER(pthread_mutex_lock, RTLD_NEXT);
    SET_WRAPPER(pthread_mutex_unlock, RTLD_NEXT);
    SET_WRAPPER(pthread_mutex_trylock, RTLD_NEXT);
    
    SET_WRAPPER(pthread_cond_wait, RTLD_NEXT);
    SET_WRAPPER(pthread_cond_timedwait, RTLD_NEXT);
    SET_WRAPPER(pthread_cond_signal, RTLD_NEXT);
    SET_WRAPPER(pthread_cond_broadcast, RTLD_NEXT);
    
    SET_WRAPPER(pthread_barrier_wait, RTLD_NEXT);
  }
}
