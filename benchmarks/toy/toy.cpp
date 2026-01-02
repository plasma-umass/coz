/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include <coz.h>
#include <thread>
#include <stdio.h>

  char padding[1024];

const unsigned long long its = 100000000ULL;

void a() {
  volatile unsigned long long x;
  for(x=0; x<its; x++) {}
}

void b() {
  volatile unsigned long long y;
  for(y=0; y<its/2; y++) {}
}

#ifndef TOY_SEQUENTIAL
#define TOY_SEQUENTIAL 0
#endif

int main() {
  printf("Starting.\n");
#if TOY_SEQUENTIAL
  printf("One thread.\n");
#else
  printf("Two threads\n");
#endif
  
  for (int i = 0; i < 100; i++) {
#if TOY_SEQUENTIAL
    a();
    b();
#else
    std::thread a_thread(a);
    std::thread b_thread(b);
    
    a_thread.join();
    b_thread.join();
#endif
    COZ_PROGRESS;
    printf(".");
    fflush(stdout);
  }
  printf("\n");
}
