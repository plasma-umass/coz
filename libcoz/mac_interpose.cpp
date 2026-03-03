/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifdef __APPLE__

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

// Forward declarations for functions defined in libcoz.cpp
extern "C" bool coz_initialized();
extern "C" void coz_pre_block();
extern "C" void coz_post_block(bool);
extern "C" void coz_catch_up();
extern "C" void coz_shutdown();
extern "C" bool coz_is_coz_signal(int signum);
extern "C" void coz_remove_coz_signals(sigset_t* set);

extern "C" {

// Thread entry function type for profiler
typedef void* (*thread_fn_t)(void*);

// ============================================================================
// Direct references to original functions using __asm
// This bypasses DYLD interposition by binding directly to the symbol
// ============================================================================
extern int orig_pthread_create(pthread_t*, const pthread_attr_t*,
                               void*(*)(void*), void*) __asm("_pthread_create");
extern void orig_pthread_exit(void*) __asm("_pthread_exit");
extern int orig_pthread_join(pthread_t, void**) __asm("_pthread_join");
extern int orig_pthread_mutex_lock(pthread_mutex_t*) __asm("_pthread_mutex_lock");
extern int orig_pthread_mutex_unlock(pthread_mutex_t*) __asm("_pthread_mutex_unlock");
extern int orig_pthread_cond_wait(pthread_cond_t*, pthread_mutex_t*) __asm("_pthread_cond_wait");
extern int orig_pthread_cond_timedwait(pthread_cond_t*, pthread_mutex_t*,
                                       const struct timespec*) __asm("_pthread_cond_timedwait");
extern int orig_pthread_cond_signal(pthread_cond_t*) __asm("_pthread_cond_signal");
extern int orig_pthread_cond_broadcast(pthread_cond_t*) __asm("_pthread_cond_broadcast");
extern int orig_pthread_rwlock_rdlock(pthread_rwlock_t*) __asm("_pthread_rwlock_rdlock");
extern int orig_pthread_rwlock_wrlock(pthread_rwlock_t*) __asm("_pthread_rwlock_wrlock");
extern int orig_pthread_rwlock_unlock(pthread_rwlock_t*) __asm("_pthread_rwlock_unlock");
// exit/_exit/_Exit: can't use __asm("_exit") because that's the _exit syscall.
// Use unique asm names and resolve via linker alias.
extern void orig_libc_exit(int) __asm("_exit");
extern void orig_libc__exit(int) __asm("__exit");
extern void orig_libc__Exit(int) __asm("__Exit");
// signal: complex return type, declare as function pointer
typedef void (*sig_handler_fn)(int);
typedef sig_handler_fn (*signal_fn_t)(int, sig_handler_fn);
extern signal_fn_t orig_signal_fn __asm("_signal");
extern int orig_sigaction(int, const struct sigaction*, struct sigaction*) __asm("_sigaction");
extern int orig_sigprocmask(int, const sigset_t*, sigset_t*) __asm("_sigprocmask");
extern int orig_pthread_sigmask(int, const sigset_t*, sigset_t*) __asm("_pthread_sigmask");
extern int orig_kill(pid_t, int) __asm("_kill");
extern int orig_pthread_kill(pthread_t, int) __asm("_pthread_kill");
extern int orig_sigwait(const sigset_t*, int*) __asm("_sigwait");
extern int orig_sigsuspend(const sigset_t*) __asm("_sigsuspend");

// ============================================================================
// DYLD Interposition macro
// ============================================================================
#define DYLD_INTERPOSE(_replacement, _original) \
  __attribute__((used)) static struct { \
    const void* replacement; \
    const void* replacee; \
  } _interpose_##_original __attribute__((section("__DATA,__interpose"))) = { \
    (const void*)&_replacement, \
    (const void*)&_original \
  }

// ============================================================================
// pthread_create wrapper
// ============================================================================
int coz_pthread_create(pthread_t* thread,
                       const pthread_attr_t* attr,
                       void* (*fn)(void*),
                       void* arg) {
  if (!coz_initialized()) {
    return orig_pthread_create(thread, attr, fn, arg);
  }

  extern int coz_handle_pthread_create(pthread_t*, const pthread_attr_t*, thread_fn_t, void*);
  return coz_handle_pthread_create(thread, attr, (thread_fn_t)fn, arg);
}
DYLD_INTERPOSE(coz_pthread_create, pthread_create);

// ============================================================================
// pthread_exit wrapper
// handle_pthread_exit -> real::pthread_exit -> coz_pthread_exit would recurse
// because dlsym returns the interposed version. Thread-local guard breaks it.
// ============================================================================
void __attribute__((noreturn)) coz_pthread_exit(void* result) {
  static thread_local bool in_exit = false;
  if (!in_exit && coz_initialized()) {
    in_exit = true;
    extern void coz_handle_pthread_exit(void*);
    coz_handle_pthread_exit(result);
    // handle_pthread_exit doesn't return
  }
  orig_pthread_exit(result);
  __builtin_unreachable();
}
DYLD_INTERPOSE(coz_pthread_exit, pthread_exit);

// ============================================================================
// pthread_join wrapper
// ============================================================================
int coz_pthread_join(pthread_t t, void** retval) {
  if (!coz_initialized()) {
    return orig_pthread_join(t, retval);
  }

  coz_pre_block();
  int result = orig_pthread_join(t, retval);
  coz_post_block(true);
  return result;
}
DYLD_INTERPOSE(coz_pthread_join, pthread_join);

// ============================================================================
// Mutex wrappers
// ============================================================================
int coz_pthread_mutex_lock(pthread_mutex_t* mutex) {
  if (!coz_initialized()) return orig_pthread_mutex_lock(mutex);
  coz_pre_block();
  int result = orig_pthread_mutex_lock(mutex);
  coz_post_block(true);
  return result;
}
DYLD_INTERPOSE(coz_pthread_mutex_lock, pthread_mutex_lock);

int coz_pthread_mutex_unlock(pthread_mutex_t* mutex) {
  if (coz_initialized()) coz_catch_up();
  return orig_pthread_mutex_unlock(mutex);
}
DYLD_INTERPOSE(coz_pthread_mutex_unlock, pthread_mutex_unlock);

// ============================================================================
// Condition variable wrappers
// ============================================================================
int coz_pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
  if (!coz_initialized()) return orig_pthread_cond_wait(cond, mutex);
  coz_pre_block();
  int result = orig_pthread_cond_wait(cond, mutex);
  coz_post_block(true);
  return result;
}
DYLD_INTERPOSE(coz_pthread_cond_wait, pthread_cond_wait);

int coz_pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                               const struct timespec* abstime) {
  if (!coz_initialized()) return orig_pthread_cond_timedwait(cond, mutex, abstime);
  coz_pre_block();
  int result = orig_pthread_cond_timedwait(cond, mutex, abstime);
  coz_post_block(result == 0);
  return result;
}
DYLD_INTERPOSE(coz_pthread_cond_timedwait, pthread_cond_timedwait);

int coz_pthread_cond_signal(pthread_cond_t* cond) {
  if (coz_initialized()) coz_catch_up();
  return orig_pthread_cond_signal(cond);
}
DYLD_INTERPOSE(coz_pthread_cond_signal, pthread_cond_signal);

int coz_pthread_cond_broadcast(pthread_cond_t* cond) {
  if (coz_initialized()) coz_catch_up();
  return orig_pthread_cond_broadcast(cond);
}
DYLD_INTERPOSE(coz_pthread_cond_broadcast, pthread_cond_broadcast);

// ============================================================================
// Read-write lock wrappers
// ============================================================================
int coz_pthread_rwlock_rdlock(pthread_rwlock_t* rwlock) {
  if (!coz_initialized()) return orig_pthread_rwlock_rdlock(rwlock);
  coz_pre_block();
  int result = orig_pthread_rwlock_rdlock(rwlock);
  coz_post_block(true);
  return result;
}
DYLD_INTERPOSE(coz_pthread_rwlock_rdlock, pthread_rwlock_rdlock);

int coz_pthread_rwlock_wrlock(pthread_rwlock_t* rwlock) {
  if (!coz_initialized()) return orig_pthread_rwlock_wrlock(rwlock);
  coz_pre_block();
  int result = orig_pthread_rwlock_wrlock(rwlock);
  coz_post_block(true);
  return result;
}
DYLD_INTERPOSE(coz_pthread_rwlock_wrlock, pthread_rwlock_wrlock);

int coz_pthread_rwlock_unlock(pthread_rwlock_t* rwlock) {
  if (coz_initialized()) coz_catch_up();
  return orig_pthread_rwlock_unlock(rwlock);
}
DYLD_INTERPOSE(coz_pthread_rwlock_unlock, pthread_rwlock_unlock);

// ============================================================================
// Exit wrappers — run shutdown before exiting
// ============================================================================
void __attribute__((noreturn)) coz_exit(int status) {
  coz_shutdown();
  orig_libc_exit(status);
  __builtin_unreachable();
}
DYLD_INTERPOSE(coz_exit, exit);

void __attribute__((noreturn)) coz__exit(int status) {
  coz_shutdown();
  orig_libc__exit(status);
  __builtin_unreachable();
}
DYLD_INTERPOSE(coz__exit, _exit);

void __attribute__((noreturn)) coz__Exit(int status) {
  coz_shutdown();
  orig_libc__Exit(status);
  __builtin_unreachable();
}
DYLD_INTERPOSE(coz__Exit, _Exit);

// ============================================================================
// Signal wrappers — protect coz's required signals
// ============================================================================
sig_handler_fn coz_signal(int signum, sig_handler_fn handler) {
  if (coz_is_coz_signal(signum)) {
    return NULL;
  }
  return orig_signal_fn(signum, handler);
}
DYLD_INTERPOSE(coz_signal, signal);

int coz_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
  if (coz_is_coz_signal(signum)) {
    return 0;
  }
  if (act != NULL) {
    struct sigaction my_act = *act;
    coz_remove_coz_signals(&my_act.sa_mask);
    return orig_sigaction(signum, &my_act, oldact);
  }
  return orig_sigaction(signum, act, oldact);
}
DYLD_INTERPOSE(coz_sigaction, sigaction);

int coz_sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
  if ((how == SIG_BLOCK || how == SIG_SETMASK) && set != NULL) {
    sigset_t myset = *set;
    coz_remove_coz_signals(&myset);
    return orig_sigprocmask(how, &myset, oldset);
  }
  return orig_sigprocmask(how, set, oldset);
}
DYLD_INTERPOSE(coz_sigprocmask, sigprocmask);

int coz_pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
  if ((how == SIG_BLOCK || how == SIG_SETMASK) && set != NULL) {
    sigset_t myset = *set;
    coz_remove_coz_signals(&myset);
    return orig_pthread_sigmask(how, &myset, oldset);
  }
  return orig_pthread_sigmask(how, set, oldset);
}
DYLD_INTERPOSE(coz_pthread_sigmask, pthread_sigmask);

// ============================================================================
// kill/pthread_kill — catch up on delays before signaling
// ============================================================================
int coz_kill(pid_t pid, int sig) {
  if (coz_initialized() && pid == getpid()) coz_catch_up();
  return orig_kill(pid, sig);
}
DYLD_INTERPOSE(coz_kill, kill);

int coz_pthread_kill(pthread_t thread, int sig) {
  if (coz_initialized()) coz_catch_up();
  return orig_pthread_kill(thread, sig);
}
DYLD_INTERPOSE(coz_pthread_kill, pthread_kill);

// ============================================================================
// sigwait — filter coz signals, track blocking
// ============================================================================
int coz_sigwait(const sigset_t* set, int* sig) {
  sigset_t myset = *set;
  coz_remove_coz_signals(&myset);

  if (coz_initialized()) coz_pre_block();
  int result = orig_sigwait(&myset, sig);
  if (coz_initialized()) coz_post_block(result == 0);
  return result;
}
DYLD_INTERPOSE(coz_sigwait, sigwait);

// ============================================================================
// sigsuspend — filter coz signals, track blocking
// ============================================================================
int coz_sigsuspend(const sigset_t* set) {
  sigset_t oldset;
  int sig;
  orig_sigprocmask(SIG_SETMASK, set, &oldset);
  int rc = coz_sigwait(set, &sig);
  orig_sigprocmask(SIG_SETMASK, &oldset, nullptr);
  return rc;
}
DYLD_INTERPOSE(coz_sigsuspend, sigsuspend);

// ============================================================================
// Expose original pthread functions for use by the profiler.
// On macOS, dlsym returns the DYLD-interposed version, so the profiler
// must use these __asm-bound originals for internal thread management.
// ============================================================================
int coz_orig_pthread_create(pthread_t* thread,
                            const pthread_attr_t* attr,
                            void* (*fn)(void*),
                            void* arg) {
  return orig_pthread_create(thread, attr, fn, arg);
}

int coz_orig_pthread_join(pthread_t thread, void** retval) {
  return orig_pthread_join(thread, retval);
}

int coz_orig_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
  return orig_sigaction(signum, act, oldact);
}

int coz_orig_sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
  return orig_sigprocmask(how, set, oldset);
}

int coz_orig_pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
  return orig_pthread_sigmask(how, set, oldset);
}

} // extern "C"

#endif // __APPLE__
