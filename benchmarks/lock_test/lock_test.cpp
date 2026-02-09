#include <coz.h>
#include <pthread.h>
#include <stdio.h>

// Shared mutex — the bottleneck
static pthread_mutex_t the_lock = PTHREAD_MUTEX_INITIALIZER;

// Work inside the critical section (the bottleneck)
static volatile unsigned long long shared_counter;

void critical_work() {
  for (volatile int i = 0; i < 5000000; i++) {
    shared_counter++;  // line 14 — should be the bottleneck
  }
}

// Work outside the critical section (not the bottleneck)
void local_work() {
  volatile unsigned long long x = 0;
  for (volatile int i = 0; i < 1000000; i++) {
    x++;  // line 22 — should show ~0% impact
  }
}

struct thread_arg {
  int iterations;
};

void* worker(void* arg) {
  int iters = ((thread_arg*)arg)->iterations;
  for (int i = 0; i < iters; i++) {
    local_work();

    pthread_mutex_lock(&the_lock);
    critical_work();
    pthread_mutex_unlock(&the_lock);

    COZ_PROGRESS;
  }
  return nullptr;
}

int main() {
  const int NUM_THREADS = 4;
  const int ITERS_PER_THREAD = 500;

  printf("Lock contention test: %d threads, %d iterations each\n", NUM_THREADS, ITERS_PER_THREAD);

  pthread_t threads[NUM_THREADS];
  thread_arg args[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].iterations = ITERS_PER_THREAD;
    pthread_create(&threads[i], nullptr, worker, &args[i]);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], nullptr);
  }

  printf("Done.\n");
}
