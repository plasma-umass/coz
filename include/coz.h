/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#if !defined(COZ_H)
#define COZ_H

#ifndef __USE_GNU
#  define __USE_GNU
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <stdint.h>
#include <string.h> /* for memcpy hack below */

#if defined(__cplusplus)
extern "C" {
#endif

#define COZ_COUNTER_TYPE_THROUGHPUT 1
#define COZ_COUNTER_TYPE_BEGIN 2
#define COZ_COUNTER_TYPE_END 3

// Declare dlsym as a weak reference so libdl isn't required
void* dlsym(void* handle, const char* symbol) __attribute__((weak));

// Counter info struct, containing both a counter and backoff size
typedef struct {
  size_t count;    // The actual count
  size_t backoff;  // Used to batch updates to the shared counter. Currently unused.
} coz_counter_t;

// The type of the _coz_get_counter function
typedef coz_counter_t* (*coz_get_counter_t)(int, const char*);

// The type of the _coz_add_delays function
typedef void (*coz_add_delays_t)(void);

// The type of the _coz_pre_block function
typedef void (*coz_pre_block_t)(void);

// The type of the _coz_post_block function
typedef void (*coz_post_block_t)(int);

// Locate and invoke _coz_get_counter
static coz_counter_t* _call_coz_get_counter(int type, const char* name) {
  static unsigned char _initialized = 0;
  static coz_get_counter_t fn; // The pointer to _coz_get_counter

  if(!_initialized) {
    if(dlsym) {
      // Locate the _coz_get_counter method
      void* p = dlsym(RTLD_DEFAULT, "_coz_get_counter");

      // Use memcpy to avoid pedantic GCC complaint about storing function pointer in void*
      memcpy(&fn, &p, sizeof(p));
    }

    _initialized = 1;
  }

  // Call the function, or return null if profiler is not found
  if(fn) return fn(type, name);
  else return 0;
}

// Locate and invoke _coz_add_delays
// This ensures worker threads check their delay debt at progress points,
// which is critical on macOS where per-thread timers are not available.
static void _call_coz_add_delays(void) {
  static unsigned char _initialized = 0;
  static coz_add_delays_t fn; // The pointer to _coz_add_delays

  if(!_initialized) {
    if(dlsym) {
      // Locate the _coz_add_delays method
      void* p = dlsym(RTLD_DEFAULT, "_coz_add_delays");

      // Use memcpy to avoid pedantic GCC complaint about storing function pointer in void*
      memcpy(&fn, &p, sizeof(p));
    }

    _initialized = 1;
  }

  // Call the function if profiler is found
  if(fn) fn();
}

// Locate and invoke _coz_pre_block
static void _call_coz_pre_block(void) {
  static unsigned char _initialized = 0;
  static coz_pre_block_t fn;

  if(!_initialized) {
    if(dlsym) {
      void* p = dlsym(RTLD_DEFAULT, "_coz_pre_block");
      memcpy(&fn, &p, sizeof(p));
    }
    _initialized = 1;
  }

  if(fn) fn();
}

// Locate and invoke _coz_post_block
static void _call_coz_post_block(int skip_delays) {
  static unsigned char _initialized = 0;
  static coz_post_block_t fn;

  if(!_initialized) {
    if(dlsym) {
      void* p = dlsym(RTLD_DEFAULT, "_coz_post_block");
      memcpy(&fn, &p, sizeof(p));
    }
    _initialized = 1;
  }

  if(fn) fn(skip_delays);
}

// On macOS, per-thread timers are not available so worker threads must check
// their delay debt at progress points.  On Linux, delays are already applied
// in the SIGPROF handler via process_samples() -> add_delays(), so calling
// add_delays() again at every progress-point hit causes double application
// and TPS collapse under high concurrency.
#ifdef __APPLE__
#  define _COZ_CHECK_DELAYS _call_coz_add_delays()
#else
#  define _COZ_CHECK_DELAYS ((void)0)
#endif

// Macro to initialize and increment a counter, then check for pending delays.
#define COZ_INCREMENT_COUNTER(type, name) \
  if(1) { \
    static unsigned char _initialized = 0; \
    static coz_counter_t* _counter = 0; \
    \
    if(!_initialized) { \
      _counter = _call_coz_get_counter(type, name); \
      _initialized = 1; \
    } \
    if(_counter) { \
      __atomic_add_fetch(&_counter->count, 1, __ATOMIC_RELAXED); \
      _COZ_CHECK_DELAYS; \
    } \
  }

#define STR2(x) #x 
#define STR(x) STR2(x)

#define COZ_PROGRESS_NAMED(name) COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_THROUGHPUT, name)

#define COZ_PROGRESS COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_THROUGHPUT, __FILE__ ":" STR(__LINE__))
#define COZ_BEGIN(name) COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_BEGIN, name)
#define COZ_END(name) COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_END, name)

// Custom synchronization support.
// Use these macros around blocking operations that Coz does not intercept
// (e.g., custom mutexes, futex-based locks, RocksDB internal synchronization).
//
//   COZ_PRE_BLOCK;                        // before blocking
//   my_custom_lock_acquire(&lock);
//   COZ_POST_BLOCK(1);                    // after blocking (1 = skip delays)
//
//   // Before potentially unblocking another thread:
//   COZ_CATCH_UP;
//   my_custom_lock_release(&lock);
//
// COZ_POST_BLOCK(skip_delays):
//   skip_delays=1 when woken by another thread (e.g., mutex acquired)
//   skip_delays=0 when the wake may have been spurious or timed out
#define COZ_PRE_BLOCK _call_coz_pre_block()
#define COZ_CATCH_UP _call_coz_add_delays()
#define COZ_POST_BLOCK(skip_delays) _call_coz_post_block(skip_delays)

#if defined(__cplusplus)
}
#endif

#endif
