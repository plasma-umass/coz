/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include <coz.h>
#include <thread>
#include <stdio.h>

  volatile size_t x;
  char padding[1024];
  volatile size_t y;


void a() {
  for(x=0; x<1000000000; x++) {}  // 100M - fast function
}

void b() {
  for(y=0; y<2000000000; y++) {}  // 200M - slow function (bottleneck)
}

int main() {
  for (int i = 0; i < 10; i++) {
    std::thread a_thread(a);
    std::thread b_thread(b);
    
    a_thread.join();
    b_thread.join();
    COZ_PROGRESS;
    printf(".");
    fflush(stdout);
  }
  printf("\n");
}
