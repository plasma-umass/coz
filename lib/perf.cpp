#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>

#include "perf.h"

long perf_event_open(struct perf_event_attr *hw_event, 
                     pid_t pid, int cpu,
                     int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

void startSampling(size_t cycles, int signum) {
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(struct perf_event_attr));
  
  pe.type = PERF_TYPE_HARDWARE;
  pe.config = PERF_COUNT_HW_CPU_CYCLES;
  pe.size = sizeof(struct perf_event_attr);
  pe.inherit = 0;
  
  pe.sample_period = cycles;
  pe.sample_type = PERF_SAMPLE_IP;
  
  int fd = perf_event_open(&pe, 0, -1, -1, 0);
  if (fd == -1) {
    fprintf(stderr, "Error opening leader %llx\n", pe.config);
    abort();
  }
  
  if(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_ASYNC) == -1)
    perror("fcntl:");
  
  if(fcntl(fd, F_SETSIG, signum) == -1)
    perror("fcntl:");

  if(fcntl(fd, F_SETOWN, getpid()) == -1)
    perror("fcntl:");

  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
}
