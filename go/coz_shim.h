/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#ifndef COZ_GO_SHIM_H
#define COZ_GO_SHIM_H

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

/* Resolve _coz_get_counter/_coz_add_delays from the injected libcoz. Idempotent
 * but not thread-safe; callers must serialize (the Go side uses sync.Once). */
void coz_shim_init(void);

/* Non-zero if libcoz was found, i.e. the program is running under `coz run`. */
int coz_shim_available(void);

/* Returns libcoz's counter for (type, name), or NULL if not profiling. */
coz_counter_t* coz_shim_get_counter(int type, const char* name);

/* Calls _coz_add_delays() if available. See checkDelays() in the Go sources for
 * why this is only invoked on macOS. */
void coz_shim_add_delays(void);

#endif /* COZ_GO_SHIM_H */
