/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

/* RTLD_DEFAULT is a GNU extension on glibc. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "coz_shim.h"

#include <dlfcn.h>
#include <stddef.h>

typedef coz_counter_t* (*coz_get_counter_t)(int, const char*);
typedef void (*coz_add_delays_t)(void);

static coz_get_counter_t s_get_counter;
static coz_add_delays_t s_add_delays;

void coz_shim_init(void) {
  /* libcoz is injected by `coz run` via LD_PRELOAD / DYLD_INSERT_LIBRARIES, so
   * its symbols are in the global scope and RTLD_DEFAULT finds them. When the
   * program runs without the profiler these stay NULL and every entry point
   * below degrades to a no-op. */
  s_get_counter = (coz_get_counter_t)dlsym(RTLD_DEFAULT, "_coz_get_counter");
  s_add_delays = (coz_add_delays_t)dlsym(RTLD_DEFAULT, "_coz_add_delays");
}

int coz_shim_available(void) { return s_get_counter != NULL; }

coz_counter_t* coz_shim_get_counter(int type, const char* name) {
  if (s_get_counter == NULL) return NULL;
  return s_get_counter(type, name);
}

void coz_shim_add_delays(void) {
  if (s_add_delays != NULL) s_add_delays();
}
