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
#include <linux/perf_event.h>
#include <errno.h>

typedef struct perf_event_attr perf_event_attr;

// syscall wrapper for perf_event_open
// 올바른 인자 for cgroup
// pid : cgroup 디렉터리 FD
// cpu 인자 : 0,1,2 ...
// group_fd : -1 (단일이벤트), 리더 FD (그룹화 이벤트)
// flags : PERF_FLAG_PID_CGROUP (이걸 통해 커널이 pid를 cgroup FD로 해석)
static long perf_event_open(struct perf_event_attr *attr,
                             int pid, int cpu, int group_fd, unsigned long flags) {
    std::cout << "   >> perf_event_open : pid=" << pid << " cpu=" << cpu << " groupd_fd=" << group_fd << std::endl;
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

// Get list of online CPUs
static std::vector<int> online_cpus() {
    int n = sysconf(_SC_NPROCESSORS_ONLN);
    std::vector<int> cpus;
    for(int i = 0; i < n; i++) cpus.push_back(i);
    std::cerr << "# CPUs detected: " << cpus.size() << std::endl;
    return cpus;
}

// Sample perf events for a cgroup on the first online CPU
// sampler를 초기화하는 로직
int perf_sampler_sync(int cg_fd,
                      std::chrono::milliseconds period,
                      double delta,
                      const std::vector<cgroup>& others,
                      const std::string& mode) {
    (void)delta; (void)others; (void)mode;

    std::cout << "In >> perf_sampler_sync" << std::endl;
    // Configure perf_event_attr for SW task-clock
    perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type           = PERF_TYPE_SOFTWARE;
    pe.size           = sizeof(pe);
    pe.config         = PERF_COUNT_SW_TASK_CLOCK;
    pe.disabled       = 1;
    pe.exclude_kernel = 1;
    pe.exclude_idle   = 1;
    pe.sample_period  = period.count() * 1000000ULL; // ms -> ns
    pe.sample_type    = PERF_SAMPLE_IP;
    pe.wakeup_events  = 1;
    std::cout << "   >> perf_sampler_sync : attr 설정 완료" << std::endl;


    // Open perf event on first CPU
    auto cpus = online_cpus();
    if(cpus.empty()) {
        std::cerr << "Error: No online CPUs" << std::endl;
        return -1;
    }
    int cpu = cpus[0];
    // std::cout << "   >> perf_sampler_sync : pid : " << cg_fd << std::endl;
    int fd = (int)perf_event_open(&pe, cg_fd, cpu, -1, PERF_FLAG_PID_CGROUP | PERF_FLAG_FD_CLOEXEC);
    if (fd < 0) {
        int val = -1;
        FILE* f = fopen("/proc/sys/kernel/perf_event_paranoid", "r");
        if(f) { fscanf(f, "%d", &val); fclose(f); }
        std::cerr << "perf_event_open failed (paranoid=" << val << "): "
                  << strerror(errno) << std::endl;
        return -1;
    }

    // Reset and enable counter
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

    // Poll for one interval
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, (int)period.count());
    if(ret < 0) {
        perror("poll");
    } else if(pfd.revents & POLLIN) {
        std::cerr << "cgroup activity detected on CPU " << cpu << std::endl;
    }

    // Disable and close
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    close(fd);
    return 0;
}