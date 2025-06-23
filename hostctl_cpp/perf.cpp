#include "perf.h"
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <chrono>

struct perf_event_attr {
    uint32_t type;
    uint32_t size;
    uint64_t config;
    uint64_t sample_type;
    uint64_t sample_period;
    uint64_t sample_freq;
    uint32_t wakeup_events;
    uint32_t wakeup_watermark;
    uint32_t bp_type;
    uint32_t reserved1;
    uint64_t bp_addr;
    uint64_t bp_len;
    uint64_t branch_sample_type;
    uint64_t sample_regs_user;
    uint32_t sample_stack_user;
    int32_t  clockid;
    uint64_t sample_regs_intr;
    uint32_t aux_watermark;
    uint32_t reserved2;
    uint64_t flags;
};

#define PERF_TYPE_HARDWARE        0
#define PERF_COUNT_HW_INSTRUCTIONS 0
#define PERF_SAMPLE_IP            (1U << 0)
#define PERF_FLAG_PID_CGROUP      (1U << 2)
#define PERF_FLAG_FD_CLOEXEC      (1U << 3)

#define PERF_ATTR_FLAG_DISABLED       (1U << 0)
#define PERF_ATTR_FLAG_ENABLE_ON_EXEC (1U << 7)

static int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu,
                           int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static std::vector<int> online_cpus() {
    int n = sysconf(_SC_NPROCESSORS_ONLN);
    std::vector<int> cpus;
    for(int i=0;i<n;i++) cpus.push_back(i);
    std::cerr << "# CPU = " << cpus.size() << std::endl;
    return cpus;
}

int perf_sampler_sync(int cg_fd, std::chrono::milliseconds period, double delta,
                      const std::vector<cgroup>& others, const std::string& mode) {
    (void)period; (void)delta; (void)others; (void)mode;
    perf_event_attr attr{};
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_INSTRUCTIONS;
    attr.sample_period = 1000;
    attr.sample_type = PERF_SAMPLE_IP;
    attr.flags = PERF_ATTR_FLAG_DISABLED;
    attr.wakeup_events = 1;
    attr.size = sizeof(perf_event_attr);

    std::cerr << "PerfEventAttr: type="<<attr.type
              <<" config="<<attr.config
              <<" period="<<attr.sample_period<<std::endl;

    std::vector<struct pollfd> fds;
    for(int cpu : online_cpus()) {
        int fd = perf_event_open(&attr, cg_fd, cpu, -1,
                                 PERF_FLAG_PID_CGROUP|PERF_FLAG_FD_CLOEXEC);
        if(fd < 0) {
            perror("perf_event_open");
            return -1;
        }
        ioctl(fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
        fds.push_back({fd, POLLIN, 0});
    }

    while(true) {
        int ret = poll(fds.data(), fds.size(), 1000);
        if(ret < 0) {
            perror("poll");
            continue;
        }
        for(size_t i=0;i<fds.size();i++) {
            if(fds[i].revents & POLLIN) {
                std::cerr << "cpu " << i << " -> \355\212\270\354\260\220 \354\243\274\352\246\214!" << std::endl;
            }
        }
    }
    return 0;
}

