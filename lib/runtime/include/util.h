#if !defined(CAUSAL_RUNTIME_UTIL_H)
#define CAUSAL_RUNTIME_UTIL_H

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "real.h"

/**
 * Get the current time in nanoseconds
 */
static size_t get_time() {
#if defined(__APPLE__)
  return mach_absolute_time();
#else
  struct timespec ts;
  if(clock_gettime(CLOCK_REALTIME, &ts)) {
    perror("get_time():");
    abort();
  }
  return ts.tv_nsec + ts.tv_sec * 1000 * 1000 * 1000;
#endif
}

static inline void wait(size_t ns) {
  struct timespec ts;
  ts.tv_nsec = ns % (1000 * 1000 * 1000);
  ts.tv_sec = (ns - ts.tv_nsec) / (1000 * 1000 * 1000);
  
  while(nanosleep(&ts, &ts) != 0) {}
}

static void set_signal_handler(int signum, void (*handler)(int, siginfo_t*, void*)) {
  // Set up the cycle sampler's signal handler
  struct sigaction sa;
  sa.sa_sigaction = handler;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, signum);
  Real::sigaction()(signum, &sa, nullptr);
}

static inline int rt_tgsigqueueinfo(pid_t tgid, pid_t tid, int sig, siginfo_t *uinfo) {
  return syscall(__NR_rt_tgsigqueueinfo, tgid, tid, sig, uinfo);
}

static inline pid_t gettid() {
  return syscall(__NR_gettid);
}

#endif
