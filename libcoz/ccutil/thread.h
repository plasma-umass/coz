#if !defined(CCUTIL_THREAD_H)
#define CCUTIL_THREAD_H

// gettid() is provided by glibc since version 2.30
// only create our own implementation for older glibc versions
#include <features.h>
#if !__GLIBC_PREREQ(2,30)

#include <sys/syscall.h>

static inline pid_t gettid() {
  return syscall(__NR_gettid);
}

#endif

#endif
