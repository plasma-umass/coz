/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifdef __APPLE__

#include "mac_interpose.h"

/**
 * macOS interposition support implementation.
 *
 * On macOS, coz uses DYLD_INSERT_LIBRARIES to inject the profiler library.
 * Function interposition is achieved through:
 *
 * 1. Symbol interposition: Functions defined in libcoz.cpp with the same
 *    names as system functions (e.g., pthread_create, pthread_mutex_lock)
 *    will override the system versions when the library is loaded.
 *
 * 2. DYLD interposition tuples: For more reliable interposition, the
 *    __DATA,__interpose section can be used to explicitly specify
 *    which functions to replace.
 *
 * Currently, coz relies on symbol interposition (method 1) which works
 * well with DYLD_INSERT_LIBRARIES. The interposition tuples (method 2)
 * are available via the INTERPOSE macro in mac_interpose.h if needed
 * for specific functions that don't interpose correctly.
 *
 * Note: On macOS, some functions may require explicit interposition
 * tuples due to two-level namespace lookup. If interposition issues
 * are encountered, add INTERPOSE entries here.
 */

// DYLD interposition entries for pthread functions.
// NOTE: These only work with DYLD_FORCE_FLAT_NAMESPACE=1 on macOS.
// Without flat namespace, two-level namespace lookup bypasses these entries.
// The symbol-based interposition in libcoz.cpp is used as a fallback but
// only intercepts calls made from within the libcoz library itself.

// Currently disabled - uncomment if using DYLD_FORCE_FLAT_NAMESPACE
#if 0
#include <pthread.h>
#include "profiler.h"
#include "real.h"

extern "C" {
int coz_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                       void* (*fn)(void*), void* arg) {
  typedef void* (*thread_fn_t)(void*);
  return profiler::get_instance().handle_pthread_create(thread, attr, (thread_fn_t)fn, arg);
}
void __attribute__((noreturn)) coz_pthread_exit(void* result) {
  profiler::get_instance().handle_pthread_exit(result);
}
int coz_pthread_join(pthread_t t, void** retval) {
  extern bool initialized;
  if(initialized) profiler::get_instance().pre_block();
  int result = real::pthread_join(t, retval);
  if(initialized) profiler::get_instance().post_block(true);
  return result;
}
}
INTERPOSE(coz_pthread_create, pthread_create);
INTERPOSE(coz_pthread_exit, pthread_exit);
INTERPOSE(coz_pthread_join, pthread_join);
#endif

#endif // __APPLE__
