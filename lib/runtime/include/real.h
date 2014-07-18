#if !defined(CAUSAL_RUNTIME_REAL_H)
#define CAUSAL_RUNTIME_REAL_H

#include <dlfcn.h>

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#define MAKE_WRAPPER(name, handle) \
  static decltype(::name)* name() { \
    static decltype(::name)* _fn = (decltype(::name)*)dlsym(handle, #name); \
    return _fn; \
  }

extern "C" int main(int argc, char** argv);

class real {
public:
  MAKE_WRAPPER(main, RTLD_NEXT);
  
  MAKE_WRAPPER(exit, RTLD_NEXT);
  MAKE_WRAPPER(_exit, RTLD_NEXT);
  MAKE_WRAPPER(_Exit, RTLD_NEXT);
  MAKE_WRAPPER(fork, RTLD_NEXT);
  
  MAKE_WRAPPER(sigaction, RTLD_NEXT);
  MAKE_WRAPPER(signal, RTLD_NEXT);
  MAKE_WRAPPER(sigprocmask, RTLD_NEXT);
  
  MAKE_WRAPPER(pthread_create, RTLD_NEXT);
  MAKE_WRAPPER(pthread_exit, RTLD_NEXT);
  MAKE_WRAPPER(pthread_sigmask, RTLD_NEXT);
  MAKE_WRAPPER(pthread_mutex_lock, RTLD_NEXT);
  MAKE_WRAPPER(pthread_mutex_unlock, RTLD_NEXT);
  MAKE_WRAPPER(pthread_mutex_trylock, RTLD_NEXT);
  MAKE_WRAPPER(pthread_cond_wait, RTLD_NEXT);
  MAKE_WRAPPER(pthread_cond_timedwait, RTLD_NEXT);
  MAKE_WRAPPER(pthread_cond_signal, RTLD_NEXT);
  MAKE_WRAPPER(pthread_cond_broadcast, RTLD_NEXT);
};

#endif
