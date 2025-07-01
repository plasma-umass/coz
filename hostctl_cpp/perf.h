#ifndef HOSTCTL_PERF_H
#define HOSTCTL_PERF_H
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>

struct cgroup { std::string path; uint64_t id; };

/*  ── 함수 원형 ── */
void init_victims(const std::vector<cgroup>&);
int  perf_sampler_sync(int, std::chrono::milliseconds, double,
                       const std::vector<cgroup>&, const std::string&);
void cleanup();
void sigint_handler(int);          // ← 여기만 남김

#endif  // HOSTCTL_PERF_H
