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

static coz_counter_t* _call_coz_get_counter(int type, const char* name) {
  // Use memcpy to avoid pedantic GCC complaint about storing function pointer in void*
  static unsigned char initialized = dlsym == NULL;
  static coz_counter_t* (*fn)(int, const char*);

  if (!initialized) {
    void* p = dlsym(RTLD_DEFAULT, "_coz_get_counter");
    memcpy(&fn, &p, sizeof(p));
    initialized = true;
  }

  if (fn) return fn(type, name);
  else return 0;
}

static void _call_coz_pre_block() {
  static unsigned char initialized = dlsym == 0;
  static void (*fn)();

  if (!initialized) {
    void* p = dlsym(RTLD_DEFAULT, "_coz_pre_block");
    memcpy(&fn, &p, sizeof(p));
    initialized = true;
  }
  if (fn) return fn();
}

static void _call_coz_post_block(bool skip_delays) {
  static unsigned char initialized = dlsym == 0;
  static void (*fn)(bool);

  if (!initialized) {
    void* p = dlsym(RTLD_DEFAULT, "_coz_post_block");
    memcpy(&fn, &p, sizeof(p));
    initialized = true;
  }
  if (fn) return fn(skip_delays);
}

static void _call_coz_wake_other() {
  static unsigned char initialized = dlsym == 0;
  static void (*fn)();

  if (!initialized) {
    void* p = dlsym(RTLD_DEFAULT, "_coz_wake_other");
    memcpy(&fn, &p, sizeof(p));
    initialized = true;
  }
  if (fn) return fn();
}

// Macro to initialize and increment a counter
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
    } \
  }

#define STR2(x) #x
#define STR(x) STR2(x)

/// Indicate progress for the counter with the given name.
///
/// Example:
/// ```
/// while (more_events()) {
///     COZ_PROGRESS_NAMED("event_processed");
///     process_event();
/// }
/// ```
#define COZ_PROGRESS_NAMED(name) COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_THROUGHPUT, name)

/// Indicate progress for the counter named implicitly after the file and line
/// number it is placed on.
///
/// Example:
/// ```
/// while (more_events()) {
///     COZ_PROGRESS;
///     process_event();
/// }
/// ```
#define COZ_PROGRESS COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_THROUGHPUT, __FILE__ ":" STR(__LINE__))
#define COZ_BEGIN(name) COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_BEGIN, name)
#define COZ_END(name) COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_END, name)

/// Call before (possibly) blocking (e.g. if you're about to maybe block on a
/// futex).
///
/// Example:
/// ```
/// lock() {
///   int expected = 0;
///   if (!std::atomic_compare_exchange_weak(&futex, &expected, 1)) {
///     COZ_PRE_BLOCK();
///     syscall(SYS_futex, &futex, FUTEX_WAIT, ...);
///     COZ_POST_BLOCK();
///   }
/// }
/// ```
#define COZ_PRE_BLOCK() _call_coz_pre_block()

/// Call after unblocking. If skip_delays is true, all delays inserted during
/// the blocked period will be skipped.
///
/// Example:
/// ```
/// lock() {
///   int expected = 0;
///   if (!std::atomic_compare_exchange_weak(&futex, &expected, 1)) {
///     COZ_PRE_BLOCK();
///     syscall(SYS_futex, &futex, FUTEX_WAIT, ...);
///     COZ_POST_BLOCK();
///   }
/// }
/// ```
#define COZ_POST_BLOCK(skip_delays) _call_coz_post_block(skip_delays)

/// Ensure a thread has executed all the required delays before possibly
/// unblocking another thread.
///
/// Example:
/// ```
/// // Unlocking the futex.
/// unlock() {
///   let have_waiters = ...;
///   std::atomic_store(&futex, 0);
///   if (have_waiters) {
///     COZ_WAKE_OTHER();
///     syscall(SYS_FUTEX, &futex, FUTEX_WAKE, ...);
///   }
/// }
/// ```
#define COZ_WAKE_OTHER() _call_coz_wake_other()

#if defined(__cplusplus)
}
#endif

#endif
