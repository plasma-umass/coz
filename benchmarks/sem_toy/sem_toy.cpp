/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

// Like benchmarks/toy, but the main thread joins its workers through a
// semaphore rather than pthread_join.
//
// This is the regression test for semaphore interposition. A thread blocked on
// a semaphore is not running, so it must not be charged for virtual delays
// inserted while it slept. Before libcoz wrapped sem_wait/semaphore_wait, the
// main thread -- the one that visits the progress point -- paid all of them on
// wake-up, and the profile came out with a slope near zero or negative
// (measured: +0.13 with R^2 0.01, and -0.57 with R^2 0.18) instead of the ~1.0
// this program should show.
//
// Both loops inline the same xorshift, so the expected result is a single hot
// line with a slope near 1.0: removing that work removes the program.
#include <coz.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
static dispatch_semaphore_t done;
static void sem_setup() { done = dispatch_semaphore_create(0); }
static void sem_wait_one() { dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER); }
static void sem_signal() { dispatch_semaphore_signal(done); }
#else
#include <semaphore.h>
static sem_t done;
static void sem_setup() { sem_init(&done, 0, 0); }
static void sem_wait_one() { sem_wait(&done); }
static void sem_signal() { sem_post(&done); }
#endif

static const uint64_t kIterations = 40000000ULL;
static volatile uint64_t slow_sink, fast_sink;

static uint64_t xorshift(uint64_t v) {
  v ^= v << 13; v ^= v >> 7; v ^= v << 17; return v;
}

static void* slow_work(void*) {
  uint64_t acc = 0x9E3779B97F4A7C15ULL;
  for (uint64_t i = 0; i < kIterations; i++) acc = xorshift(acc);
  slow_sink = acc;
  sem_signal();
  return nullptr;
}

static void* fast_work(void*) {
  uint64_t acc = 0x9E3779B97F4A7C15ULL;
  for (uint64_t i = 0; i < kIterations / 2; i++) acc = xorshift(acc);
  fast_sink = acc;
  sem_signal();
  return nullptr;
}

int main() {
  sem_setup();
  printf("Starting.\n");
  for (int round = 0; round < 100; round++) {
    pthread_t a, b;
    pthread_create(&a, nullptr, slow_work, nullptr);
    pthread_create(&b, nullptr, fast_work, nullptr);
    sem_wait_one();   // main blocks here -- invisible to coz without the wrappers
    sem_wait_one();
    pthread_detach(a);
    pthread_detach(b);
    COZ_PROGRESS;
    printf("."); fflush(stdout);
  }
  printf("\nDone. %llu %llu\n", (unsigned long long)slow_sink, (unsigned long long)fast_sink);
}
