#include <asm/unistd.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "perf.h"

long perf_event_open(struct perf_event_attr *hw_event, 
                     pid_t pid, int cpu,
                     int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

pid_t gettid() {
  return syscall(__NR_gettid);
}

__thread timer_t timer;

void startSampling_new(size_t cycles, int signum) {
  struct sigevent ev;
  memset(&ev, 0, sizeof(struct sigevent));
  
  // Signal a specific thread when the timer expires
  ev.sigev_notify = SIGEV_THREAD_ID;
  ev.sigev_signo = signum;
  ev._sigev_un._tid = gettid();
  
  // Create a timer for this thread
  if(timer_create(CLOCK_REALTIME, &ev, &timer) != 0) {
    perror("timer_create:");
    abort();
  }
  
  // Set up the timer's interval
  struct itimerspec ts;
  // Set the intial timer value
  ts.it_value.tv_nsec = cycles % (1000 * 1000 * 1000);
  ts.it_value.tv_sec = (cycles - ts.it_value.tv_nsec) / (1000 * 1000 * 1000);
  // Set the timer's interval
  ts.it_interval = ts.it_value;
  
  // Start the timer
  if(timer_settime(timer, 0, &ts, NULL) != 0) {
    perror("timer_settime:");
    abort();
  }
}

/// The file descriptor for the cycle sampler perf event
__thread int cycle_sample_fd;

void startSampling(size_t cycles, int signum) {
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(struct perf_event_attr));
  
  pe.type = PERF_TYPE_HARDWARE;
  pe.config = PERF_COUNT_HW_CPU_CYCLES;
  pe.size = sizeof(struct perf_event_attr);
  
  pe.sample_period = cycles;
  pe.sample_type = PERF_SAMPLE_IP;
  
  cycle_sample_fd = perf_event_open(&pe, 0, -1, -1, 0);
  if (cycle_sample_fd == -1) {
    fprintf(stderr, "Error opening leader %llx\n", pe.config);
    abort();
  }
  
  if(fcntl(cycle_sample_fd, F_SETFL, fcntl(cycle_sample_fd, F_GETFL, 0) | O_ASYNC) == -1)
    perror("fcntl:");
  
  if(fcntl(cycle_sample_fd, F_SETSIG, signum) == -1)
    perror("fcntl:");

  if(fcntl(cycle_sample_fd, F_SETOWN, getpid()) == -1)
    perror("fcntl:");

  ioctl(cycle_sample_fd, PERF_EVENT_IOC_ENABLE, 0);
}

/// The program counter address where the trip counter is currently set
__thread uintptr_t trip_count_pc;
/// The number of trips recorded at the last call
__thread long long last_count;
/// The perf event file descriptor for the trip counter
__thread int trip_count_fd;
/// Set to the current thread ID to ensure 
__thread pthread_t trip_count_thread_id;

long long getTripCount(uintptr_t pc) {
  // Does the requested PC match the current trip counter?
  if(trip_count_pc != pc || trip_count_thread_id != pthread_self()) {
    // Has a trip counter been set up already?
    if(trip_count_thread_id == pthread_self()) {
      // Close the old trip counter's perf event file
      close(trip_count_fd);
    }

    // Set up a new perf event file
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
  
    // Set a breakpoint event type
    pe.type = PERF_TYPE_BREAKPOINT;
    pe.size = sizeof(struct perf_event_attr);
  
    // Trap on exec only
    pe.bp_type = HW_BREAKPOINT_X;
    pe.bp_len = sizeof(long);
    pe.bp_addr = pc;

    // Open the perf event
    trip_count_fd = perf_event_open(&pe, 0, -1, -1, 0);
    if(trip_count_fd == -1) {
      fprintf(stderr, "Error opening leader %llx\n", pe.config);
      abort();
    }
    
    // Set the PC, thread id, and observed count for this thread's trip counter
    trip_count_pc = pc;
    trip_count_thread_id = pthread_self();
    last_count = 0;

    // Enable the counter
    ioctl(trip_count_fd, PERF_EVENT_IOC_ENABLE, 0);
  }
  
  long long count;
  read(trip_count_fd, &count, sizeof(long long));
  long long new_visits = count - last_count;
  last_count = count;
  return new_visits;
}

void shutdownPerf() {
  //timer_delete(timer);
  close(cycle_sample_fd);
  trip_count_pc = 0;
  if(trip_count_thread_id == pthread_self()) {
    close(trip_count_fd);
  }
}
