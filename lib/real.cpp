#include "log.h"
#include "real.h"

#include <dlfcn.h>

#define DEFINE_WRAPPER(name) decltype(::name)* name;
#define SET_WRAPPER(name, handle) name = (decltype(::name)*)dlsym(handle, #name)

namespace real {
  DEFINE_WRAPPER(main);

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
  
  DEFINE_WRAPPER(pthread_mutex_lock);
  DEFINE_WRAPPER(pthread_mutex_unlock);
  DEFINE_WRAPPER(pthread_mutex_trylock);
  
  DEFINE_WRAPPER(pthread_cond_init);
  DEFINE_WRAPPER(pthread_cond_wait);
  DEFINE_WRAPPER(pthread_cond_timedwait);
  DEFINE_WRAPPER(pthread_cond_signal);
  DEFINE_WRAPPER(pthread_cond_broadcast);
  DEFINE_WRAPPER(pthread_cond_destroy);
  
  DEFINE_WRAPPER(pthread_barrier_wait);

  void init() {
    SET_WRAPPER(main, RTLD_NEXT);

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

    void *pthread_handle = dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
  	REQUIRE(pthread_handle != nullptr) << "Unable to load libpthread.so.0";

    SET_WRAPPER(pthread_create, pthread_handle);
    SET_WRAPPER(pthread_exit, pthread_handle);
    SET_WRAPPER(pthread_join, pthread_handle);
    SET_WRAPPER(pthread_sigmask, pthread_handle);
    SET_WRAPPER(pthread_kill, pthread_handle);
    
    SET_WRAPPER(pthread_mutex_lock, pthread_handle);
    SET_WRAPPER(pthread_mutex_unlock, pthread_handle);
    SET_WRAPPER(pthread_mutex_trylock, pthread_handle);
    
    SET_WRAPPER(pthread_cond_init, pthread_handle);
    SET_WRAPPER(pthread_cond_wait, pthread_handle);
    SET_WRAPPER(pthread_cond_timedwait, pthread_handle);
    SET_WRAPPER(pthread_cond_signal, pthread_handle);
    SET_WRAPPER(pthread_cond_broadcast, pthread_handle);
    SET_WRAPPER(pthread_cond_destroy, pthread_handle);
    
    SET_WRAPPER(pthread_barrier_wait, pthread_handle);
  }
}
