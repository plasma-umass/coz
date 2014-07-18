#include "real.h"

#include <dlfcn.h>

#define DEFINE_WRAPPER(name) decltype(::name)* name;
#define SET_WRAPPER(name) name = (decltype(::name)*)dlsym(RTLD_NEXT, #name)

namespace real {
  DEFINE_WRAPPER(main);

  DEFINE_WRAPPER(exit);
  DEFINE_WRAPPER(_exit);
  DEFINE_WRAPPER(_Exit);
  DEFINE_WRAPPER(fork);

  DEFINE_WRAPPER(sigaction);
  DEFINE_WRAPPER(signal);
  DEFINE_WRAPPER(sigprocmask);

  DEFINE_WRAPPER(pthread_create);
  DEFINE_WRAPPER(pthread_exit);
  DEFINE_WRAPPER(pthread_join);
  DEFINE_WRAPPER(pthread_sigmask);
  
  DEFINE_WRAPPER(pthread_mutex_lock);
  DEFINE_WRAPPER(pthread_mutex_unlock);
  DEFINE_WRAPPER(pthread_mutex_trylock);
  
  DEFINE_WRAPPER(pthread_cond_init);
  DEFINE_WRAPPER(pthread_cond_wait);
  DEFINE_WRAPPER(pthread_cond_timedwait);
  DEFINE_WRAPPER(pthread_cond_signal);
  DEFINE_WRAPPER(pthread_cond_broadcast);
  DEFINE_WRAPPER(pthread_cond_destroy);

  void init() {
    SET_WRAPPER(main);

    SET_WRAPPER(exit);
    SET_WRAPPER(_exit);
    SET_WRAPPER(_Exit);
    SET_WRAPPER(fork);

    SET_WRAPPER(sigaction);
    SET_WRAPPER(signal);
    SET_WRAPPER(sigprocmask);

    SET_WRAPPER(pthread_create);
    SET_WRAPPER(pthread_exit);
    SET_WRAPPER(pthread_join);
    SET_WRAPPER(pthread_sigmask);
    
    SET_WRAPPER(pthread_mutex_lock);
    SET_WRAPPER(pthread_mutex_unlock);
    SET_WRAPPER(pthread_mutex_trylock);
    
    SET_WRAPPER(pthread_cond_init);
    SET_WRAPPER(pthread_cond_wait);
    SET_WRAPPER(pthread_cond_timedwait);
    SET_WRAPPER(pthread_cond_signal);
    SET_WRAPPER(pthread_cond_broadcast);
    SET_WRAPPER(pthread_cond_destroy);
  }
}
