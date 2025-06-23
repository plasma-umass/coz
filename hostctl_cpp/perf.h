#ifndef HOSTCTL_PERF_H
#define HOSTCTL_PERF_H

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>

struct cgroup {
    std::string path;
    uint64_t id;
};

int perf_sampler_sync(int cg_fd, std::chrono::milliseconds period, double delta,
                      const std::vector<cgroup>& others, const std::string& mode);

#endif
