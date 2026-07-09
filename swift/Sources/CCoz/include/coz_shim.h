/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifndef COZ_SWIFT_SHIM_H
#define COZ_SWIFT_SHIM_H

#include <stddef.h>

/* Mirrors coz_counter_t in include/coz.h. */
typedef struct {
  size_t count;
  size_t backoff;
} coz_counter_t;

/* Counter types, matching include/coz.h. */
#define COZ_SHIM_THROUGHPUT 1
#define COZ_SHIM_BEGIN 2
#define COZ_SHIM_END 3

/* Non-zero if libcoz was found, i.e. the program runs under `coz run`. */
int coz_shim_available(void);

/* libcoz's counter for (type, name), or NULL when not profiling. Goes through
 * libcoz's own name table on every call; prefer coz_shim_cached_counter. */
coz_counter_t *coz_shim_get_counter(int type, const char *name);

/* As above, but memoized on this side so a hot progress point does not pay for
 * libcoz's lock and name lookup on every hit. */
coz_counter_t *coz_shim_cached_counter(int type, const char *name);

/* Record one visit to a progress point. NULL-safe.
 *
 * Mirrors COZ_INCREMENT_COUNTER in include/coz.h, including the rule that
 * _coz_add_delays() is only called on macOS. */
void coz_shim_hit(coz_counter_t *counter);

/* Custom synchronization support, mirroring COZ_PRE_BLOCK / COZ_POST_BLOCK /
 * COZ_CATCH_UP in include/coz.h. Use these around blocking primitives coz does
 * not interpose (futexes, DispatchSemaphore, Mach semaphores). All are no-ops
 * when not profiling. */
void coz_shim_pre_block(void);
void coz_shim_post_block(int skip_delays);
void coz_shim_catch_up(void);

#endif /* COZ_SWIFT_SHIM_H */
