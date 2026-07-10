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
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef coz_counter_t *(*coz_get_counter_t)(int, const char *);
typedef void (*coz_add_delays_t)(void);
typedef void (*coz_pre_block_t)(void);
typedef void (*coz_post_block_t)(int);

static coz_get_counter_t s_get_counter;
static coz_add_delays_t s_add_delays;
static coz_pre_block_t s_pre_block;
static coz_post_block_t s_post_block;

static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static void coz_shim_resolve(void) {
  /* libcoz is injected by `coz run` via LD_PRELOAD / DYLD_INSERT_LIBRARIES, so
   * its symbols live in the global scope and RTLD_DEFAULT finds them. Without
   * the profiler these stay NULL and every entry point degrades to a no-op. */
  s_get_counter = (coz_get_counter_t)dlsym(RTLD_DEFAULT, "_coz_get_counter");
  s_add_delays = (coz_add_delays_t)dlsym(RTLD_DEFAULT, "_coz_add_delays");
  s_pre_block = (coz_pre_block_t)dlsym(RTLD_DEFAULT, "_coz_pre_block");
  s_post_block = (coz_post_block_t)dlsym(RTLD_DEFAULT, "_coz_post_block");
}

static void coz_shim_ensure(void) { pthread_once(&s_once, coz_shim_resolve); }

int coz_shim_available(void) {
  coz_shim_ensure();
  return s_get_counter != NULL;
}

coz_counter_t *coz_shim_get_counter(int type, const char *name) {
  coz_shim_ensure();
  if (s_get_counter == NULL) return NULL;
  return s_get_counter(type, name);
}

/* Counter cache.
 *
 * A program has a handful of progress points, so a linked list under a mutex is
 * ample. Entries are never removed, and the counters libcoz hands back are
 * stable for the life of the process, so a hit needs no further synchronization
 * beyond the atomic increment in coz_shim_hit(). */
typedef struct coz_shim_entry {
  int type;
  char *name;
  coz_counter_t *counter;
  struct coz_shim_entry *next;
} coz_shim_entry;

static coz_shim_entry *s_cache;
static pthread_mutex_t s_cache_lock = PTHREAD_MUTEX_INITIALIZER;

coz_counter_t *coz_shim_cached_counter(int type, const char *name) {
  coz_counter_t *result = NULL;

  pthread_mutex_lock(&s_cache_lock);
  for (coz_shim_entry *e = s_cache; e != NULL; e = e->next) {
    if (e->type == type && strcmp(e->name, name) == 0) {
      result = e->counter;
      pthread_mutex_unlock(&s_cache_lock);
      return result;
    }
  }

  result = coz_shim_get_counter(type, name);

  /* Cache misses (result == NULL, i.e. not profiling) too, so that an
   * un-profiled run does not call into dlsym-resolved code on every hit. */
  coz_shim_entry *entry = (coz_shim_entry *)malloc(sizeof(coz_shim_entry));
  if (entry != NULL) {
    entry->name = strdup(name);
    if (entry->name == NULL) {
      free(entry);
    } else {
      entry->type = type;
      entry->counter = result;
      entry->next = s_cache;
      s_cache = entry;
    }
  }
  pthread_mutex_unlock(&s_cache_lock);

  return result;
}

void coz_shim_hit(coz_counter_t *counter) {
  if (counter == NULL) return;

  __atomic_add_fetch(&counter->count, 1, __ATOMIC_RELAXED);

#ifdef __APPLE__
  /* macOS has no per-thread sampling timer, so a worker thread only learns of
   * the virtual delay it owes when it reaches a progress point. On Linux the
   * SIGPROF handler already applies delays, and doing it again here would apply
   * them twice. This is the _COZ_CHECK_DELAYS split from include/coz.h. */
  if (s_add_delays != NULL) s_add_delays();
#endif
}

void coz_shim_pre_block(void) {
  coz_shim_ensure();
  if (s_pre_block != NULL) s_pre_block();
}

void coz_shim_post_block(int skip_delays) {
  coz_shim_ensure();
  if (s_post_block != NULL) s_post_block(skip_delays);
}

void coz_shim_catch_up(void) {
  coz_shim_ensure();
  if (s_add_delays != NULL) s_add_delays();
}
