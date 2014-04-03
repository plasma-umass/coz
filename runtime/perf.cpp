#include <asm/unistd.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "perf.h"

long perf_event_open(struct perf_event_attr *hw_event, 
                     pid_t pid, int cpu,
                     int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

/// The file descriptor for the cycle sampler perf event
__thread int cycle_sample_fd;

void startSampling(size_t cycles, int signum) {
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(struct perf_event_attr));
  
  pe.type = PERF_TYPE_HARDWARE;
  pe.config = PERF_COUNT_HW_CPU_CYCLES;
  pe.size = sizeof(struct perf_event_attr);
  pe.inherit = 0;
  
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
    
    fprintf(stderr, "%p: Initializing trip counter at %p\n", pthread_self(), (void*)pc);
    
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
  close(cycle_sample_fd);
  trip_count_pc = 0;
  if(trip_count_thread_id == pthread_self()) {
    close(trip_count_fd);
  }
}
