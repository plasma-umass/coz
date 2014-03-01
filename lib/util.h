#if !defined(CAUSAL_RUNTIME_UTIL_H)
#define CAUSAUL_RUNTIME_UTIL_H

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

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
  size_t end_time = getTime() + ns;
  while(getTime() < end_time) {
    __asm__("pause");
  }
}

#endif
