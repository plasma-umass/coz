/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifdef __APPLE__

#include <pthread.h>

// Forward declarations for functions defined in libcoz.cpp
extern "C" bool coz_initialized();

extern "C" {

// Thread entry function type for profiler
typedef void* (*thread_fn_t)(void*);

// ============================================================================
// Direct references to original pthread functions using __asm
// This bypasses DYLD interposition by binding directly to the symbol
// ============================================================================
extern int orig_pthread_create(pthread_t*, const pthread_attr_t*,
                               void*(*)(void*), void*) __asm("_pthread_create");
extern void orig_pthread_exit(void*) __asm("_pthread_exit");
extern int orig_pthread_join(pthread_t, void**) __asm("_pthread_join");

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

  // Call profiler's handle_pthread_create
  extern int coz_handle_pthread_create(pthread_t*, const pthread_attr_t*, thread_fn_t, void*);
  return coz_handle_pthread_create(thread, attr, (thread_fn_t)fn, arg);
}
// Temporarily disabled - causes issues with profiler thread creation
// DYLD_INTERPOSE(coz_pthread_create, pthread_create);

// ============================================================================
// pthread_exit wrapper
// ============================================================================
void __attribute__((noreturn)) coz_pthread_exit(void* result) {
  if (coz_initialized()) {
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

  extern void coz_pre_block();
  extern void coz_post_block(bool);

  coz_pre_block();
  int result = orig_pthread_join(t, retval);
  coz_post_block(true);

  return result;
}
DYLD_INTERPOSE(coz_pthread_join, pthread_join);

// ============================================================================
// Expose the original pthread_create for use by the profiler
// ============================================================================
int coz_orig_pthread_create(pthread_t* thread,
                            const pthread_attr_t* attr,
                            void* (*fn)(void*),
                            void* arg) {
  return orig_pthread_create(thread, attr, fn, arg);
}

} // extern "C"

#endif // __APPLE__
