/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include <coz.h>
#include <thread>

  volatile size_t x;
  char padding[1024];
  volatile size_t y;


void a() {
  for(x=0; x<2000000000; x++) {}
}

void b() {
  for(y=0; y<1900000000; y++) {}
}

int main() {
  for (int i = 0; i < 100; i++) {
    std::thread a_thread(a);
    std::thread b_thread(b);
    
    a_thread.join();
    b_thread.join();
    COZ_PROGRESS;
  }
}
