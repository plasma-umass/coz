/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#if !defined(CAUSAL_RUNTIME_REAL_H)
#define CAUSAL_RUNTIME_REAL_H

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#define DECLARE_WRAPPER(name) extern decltype(::name)* name;

namespace real {
  DECLARE_WRAPPER(malloc);
  DECLARE_WRAPPER(calloc);
  DECLARE_WRAPPER(free);
  DECLARE_WRAPPER(exit);
  DECLARE_WRAPPER(_exit);
  DECLARE_WRAPPER(_Exit);
  DECLARE_WRAPPER(fork);
  
  DECLARE_WRAPPER(sigaction);
  DECLARE_WRAPPER(signal);
  DECLARE_WRAPPER(kill);
  DECLARE_WRAPPER(sigprocmask);
  DECLARE_WRAPPER(sigwait);
  DECLARE_WRAPPER(sigwaitinfo);
  DECLARE_WRAPPER(sigtimedwait);
  
  DECLARE_WRAPPER(pthread_create);
  DECLARE_WRAPPER(pthread_exit);
  DECLARE_WRAPPER(pthread_join);
  DECLARE_WRAPPER(pthread_tryjoin_np);
  DECLARE_WRAPPER(pthread_timedjoin_np);
  DECLARE_WRAPPER(pthread_sigmask);
  DECLARE_WRAPPER(pthread_kill);
  DECLARE_WRAPPER(pthread_sigqueue);
  
  DECLARE_WRAPPER(pthread_mutex_lock);
  DECLARE_WRAPPER(pthread_mutex_trylock);
  DECLARE_WRAPPER(pthread_mutex_unlock);
  
  DECLARE_WRAPPER(pthread_cond_wait);
  DECLARE_WRAPPER(pthread_cond_timedwait);
  DECLARE_WRAPPER(pthread_cond_signal);
  DECLARE_WRAPPER(pthread_cond_broadcast);
  
  DECLARE_WRAPPER(pthread_barrier_wait);
  
  DECLARE_WRAPPER(pthread_rwlock_rdlock);
  DECLARE_WRAPPER(pthread_rwlock_tryrdlock);
  DECLARE_WRAPPER(pthread_rwlock_timedrdlock);
  DECLARE_WRAPPER(pthread_rwlock_wrlock);
  DECLARE_WRAPPER(pthread_rwlock_trywrlock);
  DECLARE_WRAPPER(pthread_rwlock_timedwrlock);
  DECLARE_WRAPPER(pthread_rwlock_unlock);
};

#endif
