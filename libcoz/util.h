/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#if !defined(CAUSAL_RUNTIME_UTIL_H)
#define CAUSAL_RUNTIME_UTIL_H

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <sstream>
#include <string>
#include <vector>

#include "real.h"

/**
 * Get the current time in nanoseconds
 */
static size_t get_time() {
#if defined(__APPLE__)
  return mach_absolute_time();
#else
  struct timespec ts;
  if(clock_gettime(CLOCK_REALTIME, &ts)) {
    perror("get_time():");
    abort();
  }
  return ts.tv_nsec + ts.tv_sec * 1000 * 1000 * 1000;
#endif
}

static inline size_t wait(size_t ns) {
  if(ns == 0) return 0;

  struct timespec ts;
  ts.tv_nsec = ns % (1000 * 1000 * 1000);
  ts.tv_sec = (ns - ts.tv_nsec) / (1000 * 1000 * 1000);

  size_t start_time = get_time();
  while(nanosleep(&ts, &ts) != 0) {}

  return get_time() - start_time;
}

static inline std::vector<std::string> split(const std::string& s, char delim='\t') {
  std::vector<std::string> elems;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    if(item.length()) elems.push_back(item);
  }
  return elems;
}

static inline std::string getenv_safe(const char* var, const char* fallback = "") {
  const char* value = getenv(var);
  if(!value) value = fallback;
  return std::string(value);
}

#endif
