#if !defined(CAUSAL_RUNTIME_UTIL_H)
#define CAUSAL_RUNTIME_UTIL_H

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#include <signal.h>

#include "real.h"

/**
 * Get the current time in nanoseconds
 */
static size_t getTime() {
#if defined(__APPLE__)
  return mach_absolute_time();
#else
  struct timespec ts;
  if(clock_gettime(CLOCK_REALTIME, &ts)) {
    perror("getTime():");
    abort();
  }
  return ts.tv_nsec + ts.tv_sec * 1000 * 1000 * 1000;
#endif
}

static void wait(size_t ns) {
  struct timespec ts;
  ts.tv_nsec = ns % (1000 * 1000 * 1000);
  ts.tv_sec = (ns - ts.tv_nsec) / (1000 * 1000 * 1000);
  
  while(nanosleep(&ts, &ts) != 0) {}
  
  /*size_t end_time = getTime() + ns;
  while(getTime() < end_time) {
    __asm__("pause");
  }*/
}

static void setSignalHandler(int signum, void (*handler)(int, siginfo_t*, void*), int other_signal = 0) {
  // Set up the cycle sampler's signal handler
  struct sigaction sa;
  sa.sa_sigaction = handler;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, signum);
  if(other_signal != 0) {
    sigaddset(&sa.sa_mask, other_signal);
  }
  Real::sigaction()(signum, &sa, nullptr);
}

#endif
