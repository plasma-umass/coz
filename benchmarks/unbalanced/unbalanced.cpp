#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>
#include <queue>
#include <time.h>

#include <list>

using namespace std;

enum { N = 5000 };

static void wait(size_t ns) {
  struct timespec ts;
  ts.tv_nsec = ns % (1000 * 1000 * 1000);
  ts.tv_sec = (ns - ts.tv_nsec) / (1000 * 1000 * 1000);
  
  while(nanosleep(&ts, &ts) != 0) {}
}

void foo() {
  static int x = 0;
  for(int i=0; i<N; i++) {
    x++;
  }
}

void bar() {
  static int y = 0;
  for(int i=0; i<N; i++) {
    wait(250);
    y++;
  }
}

void* thread1(void* arg) {
  foo();
  return NULL;
}

void* thread2(void* arg) {
  bar();
  return NULL;
}

int main(int argc, char** argv) {
  pthread_t t1, t2;
  
  pthread_create(&t1, NULL, thread1, NULL);
  pthread_create(&t2, NULL, thread2, NULL);
  
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);
	
	return 0;
}
