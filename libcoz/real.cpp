/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include "real.h"

#include <dlfcn.h>

#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

static bool resolving = false;        //< Set to true while symbol resolution is in progress
static bool in_dlopen = false;        //< Set to true while dlopen is running
static void* pthread_handle = NULL;   //< The `dlopen` handle to libpthread

#define GET_SYMBOL_HANDLE(name, handle) \
  decltype(::name)* real_##name = nullptr; \
  while(!__atomic_exchange_n(&resolving, true, __ATOMIC_ACQ_REL)) {} \
  uintptr_t addr = reinterpret_cast<uintptr_t>(dlsym(handle, #name)); \
  memcpy(&real_##name, &addr, sizeof(uintptr_t)); \
  if(real_##name) { \
    memcpy(&real::name, &addr, sizeof(uintptr_t)); \
  } \
  __atomic_store_n(&resolving, false, __ATOMIC_RELEASE);

#define GET_SYMBOL(name) GET_SYMBOL_HANDLE(name, RTLD_NEXT)

#define NORETURN __attribute__((noreturn))

/**
 * Get the `dlopen` handle for libpthread
 */
static void* get_pthread_handle() {
  if(pthread_handle == NULL && !__atomic_exchange_n(&in_dlopen, true, __ATOMIC_ACQ_REL)) {
    pthread_handle = dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL);
    __atomic_store_n(&in_dlopen, false, __ATOMIC_RELEASE);
  }

  return pthread_handle;
}

/*
 * Resolver functions attempt to locate the requested function. Resolution will fail if
 * it is invoked recursively, or if the symbol is not found. If resolution fails, most
 * cases an error code of -1 is returned. Synchronization options will fail silently to
 * accommodate synchronization operations during startup. If resolution succeeds, the
 * located function is called.
 */

static NORETURN void resolve_exit(int status) throw() {
  GET_SYMBOL(exit);
  if(real_exit) real_exit(status);
  abort();
}

static NORETURN void resolve__exit(int status) throw() {
  GET_SYMBOL(_exit);
  if(real__exit) real__exit(status);
  abort();
}

static NORETURN void resolve__Exit(int status) throw() {
  GET_SYMBOL(_Exit);
  if(real__Exit) real__Exit(status);
  abort();
}

static int resolve_fork() throw() {
  GET_SYMBOL(fork);
  if(real_fork) return real_fork();
  else return -1;
}

static int resolve_sigaction(int signum, const struct sigaction* act, struct sigaction* old_act) throw() {
  GET_SYMBOL(sigaction);
  if(real_sigaction) return real_sigaction(signum, act, old_act);
  else return -1;
}

static sighandler_t resolve_signal(int signum, sighandler_t handler) throw() {
  GET_SYMBOL(signal);
  if(real_signal) return real_signal(signum, handler);
  else return SIG_ERR;
}

static int resolve_kill(pid_t pid, int sig) throw() {
  GET_SYMBOL(kill);
  if(real_kill) return real_kill(pid, sig);
  else return -1;
}

static int resolve_sigprocmask(int how, const sigset_t* set, sigset_t* oldset) throw() {
  GET_SYMBOL(sigprocmask);
  if(real_sigprocmask) return real_sigprocmask(how, set, oldset);
  else return -1;
}

static int resolve_sigwait(const sigset_t* set, int* sig) {
  GET_SYMBOL(sigwait);
  if(real_sigwait) return real_sigwait(set, sig);
  else return -1;
}

static int resolve_sigwaitinfo(const sigset_t* set, siginfo_t* info) {
  GET_SYMBOL(sigwaitinfo);
  if(real_sigwaitinfo) return real_sigwaitinfo(set, info);
  else return -1;
}

static int resolve_sigtimedwait(const sigset_t* set, siginfo_t* info, const struct timespec* timeout) {
  GET_SYMBOL(sigtimedwait);
  if(real_sigtimedwait) return real_sigtimedwait(set, info, timeout);
  else return -1;
}

static int resolve_pthread_create(pthread_t* t, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) throw() {
  GET_SYMBOL_HANDLE(pthread_create, get_pthread_handle());
  if(real_pthread_create) return real_pthread_create(t, attr, fn, arg);
  else return -1;
}

static NORETURN void resolve_pthread_exit(void* retval) {
  GET_SYMBOL_HANDLE(pthread_exit, get_pthread_handle());
  if(real_pthread_exit) real_pthread_exit(retval);
  abort();
}

static int resolve_pthread_join(pthread_t t, void** ret) {
  GET_SYMBOL_HANDLE(pthread_join, get_pthread_handle());
  if(real_pthread_join) return real_pthread_join(t, ret);
  else return -1;
}

static int resolve_pthread_tryjoin_np(pthread_t t, void** ret) throw() {
  GET_SYMBOL_HANDLE(pthread_tryjoin_np, get_pthread_handle());
  if(real_pthread_tryjoin_np) return real_pthread_tryjoin_np(t, ret);
  else return -1;
}

static int resolve_pthread_timedjoin_np(pthread_t t, void** ret, const struct timespec* abstime) {
  GET_SYMBOL_HANDLE(pthread_timedjoin_np, get_pthread_handle());
  if(real_pthread_timedjoin_np) return real_pthread_timedjoin_np(t, ret, abstime);
  else return -1;
}

static int resolve_pthread_kill(pthread_t t, int sig) throw() {
  GET_SYMBOL_HANDLE(pthread_kill, get_pthread_handle());
  if(real_pthread_kill) return real_pthread_kill(t, sig);
  else return -1;
}

static int resolve_pthread_sigqueue(pthread_t t, int sig, const union sigval val) throw() {
  GET_SYMBOL_HANDLE(pthread_sigqueue, get_pthread_handle());
  if(real_pthread_sigqueue) return real_pthread_sigqueue(t, sig, val);
  else return -1;
}

static int resolve_pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) throw() {
  GET_SYMBOL_HANDLE(pthread_sigmask, get_pthread_handle());
  if(real_pthread_sigmask) return real_pthread_sigmask(how, set, oldset);
  else return -1;
}

static int resolve_pthread_mutex_lock(pthread_mutex_t* mutex) throw() {
  GET_SYMBOL_HANDLE(pthread_mutex_lock, get_pthread_handle());
  if(real_pthread_mutex_lock) return real_pthread_mutex_lock(mutex);
  else return 0;  // Silently elide locks during linking
}

static int resolve_pthread_mutex_trylock(pthread_mutex_t* mutex) throw() {
  GET_SYMBOL_HANDLE(pthread_mutex_trylock, get_pthread_handle());
  if(real_pthread_mutex_trylock) return real_pthread_mutex_trylock(mutex);
  else return 0;  // Silently elide locks during linking
}

static int resolve_pthread_mutex_unlock(pthread_mutex_t* mutex) throw() {
  GET_SYMBOL_HANDLE(pthread_mutex_unlock, get_pthread_handle());
  if(real_pthread_mutex_unlock) return real_pthread_mutex_unlock(mutex);
  else return 0;  // Silently elide locks during linking
}

static int resolve_pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) throw() {
  GET_SYMBOL_HANDLE(pthread_cond_wait, get_pthread_handle());
  if(real_pthread_cond_wait) return real_pthread_cond_wait(cond, mutex);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, const struct timespec* abstime) throw() {
  GET_SYMBOL_HANDLE(pthread_cond_timedwait, get_pthread_handle());
  if(real_pthread_cond_timedwait) return real_pthread_cond_timedwait(cond, mutex, abstime);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_cond_signal(pthread_cond_t* cond) throw() {
  GET_SYMBOL_HANDLE(pthread_cond_signal, get_pthread_handle());
  if(real_pthread_cond_signal) return real_pthread_cond_signal(cond);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_cond_broadcast(pthread_cond_t* cond) throw() {
  GET_SYMBOL_HANDLE(pthread_cond_broadcast, get_pthread_handle());
  if(real_pthread_cond_broadcast) return real_pthread_cond_broadcast(cond);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_barrier_wait(pthread_barrier_t* barr) throw() {
  GET_SYMBOL_HANDLE(pthread_barrier_wait, get_pthread_handle());
  if(real_pthread_barrier_wait) return real_pthread_barrier_wait(barr);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_rwlock_rdlock(pthread_rwlock_t* rwlock) throw() {
  GET_SYMBOL_HANDLE(pthread_rwlock_rdlock, get_pthread_handle());
  if(real_pthread_rwlock_rdlock) return real_pthread_rwlock_rdlock(rwlock);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_rwlock_tryrdlock(pthread_rwlock_t* rwlock) throw() {
  GET_SYMBOL_HANDLE(pthread_rwlock_tryrdlock, get_pthread_handle());
  if(real_pthread_rwlock_tryrdlock) return real_pthread_rwlock_tryrdlock(rwlock);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_rwlock_timedrdlock(pthread_rwlock_t* rwlock, const struct timespec* abstime) throw() {
  GET_SYMBOL_HANDLE(pthread_rwlock_timedrdlock, get_pthread_handle());
  if(real_pthread_rwlock_timedrdlock) return real_pthread_rwlock_timedrdlock(rwlock, abstime);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_rwlock_wrlock(pthread_rwlock_t* rwlock) throw() {
  GET_SYMBOL_HANDLE(pthread_rwlock_wrlock, get_pthread_handle());
  if(real_pthread_rwlock_wrlock) return real_pthread_rwlock_wrlock(rwlock);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_rwlock_trywrlock(pthread_rwlock_t* rwlock) throw() {
  GET_SYMBOL_HANDLE(pthread_rwlock_trywrlock, get_pthread_handle());
  if(real_pthread_rwlock_trywrlock) return real_pthread_rwlock_trywrlock(rwlock);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_rwlock_timedwrlock(pthread_rwlock_t* rwlock, const struct timespec* abstime) throw() {
  GET_SYMBOL_HANDLE(pthread_rwlock_timedwrlock, get_pthread_handle());
  if(real_pthread_rwlock_timedwrlock) return real_pthread_rwlock_timedwrlock(rwlock, abstime);
  else return 0;  // Silently elide synchronization during linking
}

static int resolve_pthread_rwlock_unlock(pthread_rwlock_t* rwlock) throw() {
  GET_SYMBOL_HANDLE(pthread_rwlock_unlock, get_pthread_handle());
  if(real_pthread_rwlock_unlock) return real_pthread_rwlock_unlock(rwlock);
  else return 0;  // Silently elide synchronization during linking
}

#define DEFINE_WRAPPER(name) decltype(::name)* name = &resolve_##name;

/**
 * Define all wrapper symbols in the `real` namespace. Initialize all wrappers to the
 * corresponding resolver function.
 */
namespace real {
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
  DEFINE_WRAPPER(pthread_tryjoin_np);
  DEFINE_WRAPPER(pthread_timedjoin_np);
  DEFINE_WRAPPER(pthread_sigmask);
  DEFINE_WRAPPER(pthread_kill);
  DEFINE_WRAPPER(pthread_sigqueue);

  DEFINE_WRAPPER(pthread_mutex_lock);
  DEFINE_WRAPPER(pthread_mutex_trylock);
  DEFINE_WRAPPER(pthread_mutex_unlock);

  DEFINE_WRAPPER(pthread_cond_wait);
  DEFINE_WRAPPER(pthread_cond_timedwait);
  DEFINE_WRAPPER(pthread_cond_signal);
  DEFINE_WRAPPER(pthread_cond_broadcast);

  DEFINE_WRAPPER(pthread_barrier_wait);

  DEFINE_WRAPPER(pthread_rwlock_rdlock);
  DEFINE_WRAPPER(pthread_rwlock_tryrdlock);
  DEFINE_WRAPPER(pthread_rwlock_timedrdlock);
  DEFINE_WRAPPER(pthread_rwlock_wrlock);
  DEFINE_WRAPPER(pthread_rwlock_trywrlock);
  DEFINE_WRAPPER(pthread_rwlock_timedwrlock);
  DEFINE_WRAPPER(pthread_rwlock_unlock);
}
