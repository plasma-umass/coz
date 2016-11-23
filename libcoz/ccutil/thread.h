#if !defined(CCUTIL_THREAD_H)
#define CCUTIL_THREAD_H

#include <sys/syscall.h>

static inline pid_t gettid() {
  return syscall(__NR_gettid);
}

#endif
