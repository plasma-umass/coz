#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <causal.h>

#define WIDTH 40000
#define HEIGHT 40000
#define MAX_SKIP_COUNT 8

struct thread_arg {
  size_t index;
  size_t thread_count;
  size_t skip_count;
  int* data;
  
  thread_arg(size_t i, size_t t, size_t s, int* d) : 
    index(i), thread_count(t), skip_count(s), data(d) {}
};

void* thread_fn(void* arg) {
  thread_arg* a = reinterpret_cast<thread_arg*>(arg);
  for(size_t i = a->index; i < HEIGHT; i += a->thread_count) {
    for(size_t k = 0; k < MAX_SKIP_COUNT; k++) {
      if(k >= a->skip_count) {
        for(size_t j = 0; j < WIDTH; j++) {
          a->data[i * HEIGHT + j] *= 2;
        }
      }
    }
    CAUSAL_PROGRESS;
  }
  
  delete a;
  
  return NULL;
}

size_t get_nanos() {
  struct timespec ts;
  if(clock_gettime(CLOCK_REALTIME, &ts)) {
    fprintf(stderr, "Failed to get start time\n");
    exit(2);
  }
  return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int main(int argc, char** argv) {
  if(argc != 3) {
    fprintf(stderr, "Must specify thread count and skip count on command line\n");
    exit(1);
  }
  
  size_t num_threads = atoi(argv[1]);
  size_t skip_count = atoi(argv[2]);
  int* data = new int[WIDTH * HEIGHT];
  
  size_t start_nanos = get_nanos();
  
  pthread_t threads[num_threads];
  for(size_t i = 0; i < num_threads; i++) {
    thread_arg* arg = new thread_arg(i, num_threads, skip_count, data);
    pthread_create(&threads[i], NULL, thread_fn, arg);
  }
  
  for(size_t i = 0; i < num_threads; i++) {
    while(pthread_tryjoin_np(threads[i], NULL)) {}
  }
  
  size_t nanos = get_nanos() - start_nanos;
  
  delete[] data;
  
  fprintf(stderr, "runtime: %lu\n", nanos);
  
  return 0;
}
