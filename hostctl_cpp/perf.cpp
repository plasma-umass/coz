// perf.cpp – signal-mode with verbose debug
#include "perf.h"

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <pthread.h>   // pthread_setaffinity_np
#include <signal.h>
#include <linux/perf_event.h>
#include <chrono>
#include <vector>
#include <atomic>
#include <iomanip>
#include <ctime>
#include <iostream>
#include <cstring>
#include <sys/time.h>
#include <sched.h>

/* ──────────────── global state ──────────────── */
static std::vector<int>  g_fds;                 // CPU별 perf FD
static std::atomic<bool> g_running   {true};
static std::atomic<bool> g_paused    {false};
static std::atomic<uint64_t> g_lastNs{0};       // 마지막 활동 시각(ns)
// static int kSigRT = -1;                      // SIGNAL 초기 dummy 값
static struct sigaction sa_alrm;                // freeze-unfreeze alarm용
static std::atomic<uint64_t> g_global_delay{0}; // global delay (ns)

struct cg_info { std::string path; int fd; };
static std::vector<cg_info> g_victims;
static bool is_cgv2 = (access("/sys/fs/cgroup/cgroup.controllers",F_OK)==0);

static long perf_event_open(struct perf_event_attr* attr,int pid,int cpu,int grp,unsigned long flags){
    long r=syscall(__NR_perf_event_open,attr,pid,cpu,grp,flags);
    if(r==-1) perror("perf_event_open");
    else return r;
}

static long mcoz_sleep(uint64_t ns){
    // syscall number : 449
    long r=syscall(449, ns);
    if(r!=0) perror("mcoz_delay");
}

static std::vector<int> online_cpus(){
    int n=sysconf(_SC_NPROCESSORS_ONLN); std::vector<int> v; for(int i=0;i<n;++i) v.push_back(i);
    std::cerr << "[DBG] online cpu cnt="<<v.size()<<"\n"; return v; }

/* ────── SCOZ version: per‑core handler ────── */
struct CoreHandler {
    int cpu;                    // CPU 코어
    uint64_t local_delay{0};    // local delay
    std::thread th;
    // int fd;
    // uint64_t prev {0};
};
static std::vector<CoreHandler> g_handlers;

/* - debug용 - */
double seconds_since_boot() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts); // 부팅 후 시간
    return ts.tv_sec + ts.tv_nsec / 1e9;
}


/* ───────────── sampler entry ───────────── */
int perf_sampler_sync(int cg_fd,
                      std::chrono::milliseconds period,
                      double speedup,
                      const std::vector<std::string>& others,
                      const std::string& /*mode*/)
{
    std::cerr << "[INFO] sampler start (per‑core)\n";

    /* CPU 갯수 불러옴 */
    auto cpu_cnt = sysconf(_SC_NPROCESSORS_ONLN);
    /* 각 CPU 코어별로 */
    g_handlers.reserve(cpu_cnt);

    for (int cpu = 0; cpu < cpu_cnt; ++cpu) {
        g_handlers.emplace_back(CoreHandler{cpu});
        CoreHandler& h = g_handlers.back();
        h.cpu=cpu;
        h.local_delay=0;

        h.th = std::thread([&, cg_fd, period, speedup] {

            /* ── (선택) 스레드‑CPU 바인딩 ── */
            cpu_set_t set; CPU_ZERO(&set); CPU_SET(h.cpu, &set);
            pthread_setaffinity_np(pthread_self(), sizeof(set), &set);

            /* ── 1. perf_event_open ── */
            perf_event_attr pe{}; pe.size = sizeof(pe);
            pe.type            = PERF_TYPE_SOFTWARE;
            pe.config          = PERF_COUNT_SW_TASK_CLOCK;
            pe.sample_period   = 0;                            // 샘플링 이벤트 없음 -> event_based가 아님
            pe.disabled        = 1;
            pe.read_format     = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
            pe.exclude_idle    = 1;

            int fd = perf_event_open(&pe, cg_fd, h.cpu, -1, PERF_FLAG_PID_CGROUP | PERF_FLAG_FD_CLOEXEC);
            if (fd < 0) { perror("perf_event_open - error"); return; }
            // std::cout << "[Core " << cpu << "] perf fd : " << fd << std::endl;

            if (fcntl(fd, F_SETOWN, getpid()) == -1) perror("F_SETOWN");
            int fl = fcntl(fd, F_GETFL,0);
            if (fcntl(fd, F_SETFL, fl | O_NONBLOCK | O_ASYNC) == -1) perror("F_SETFL");

            // ioctl(fd,PERF_EVENT_IOC_REFRESH,1); 
            ioctl(fd,PERF_EVENT_IOC_ENABLE,0);
            ioctl(fd,PERF_EVENT_IOC_RESET, 0 );
            
            uint64_t prev = 0, buf[3]{};

            /* ── 2. 루프를 통해 주기적으로 read ── */
            while (g_running.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 일단 1.02~1.67ms 정도

                /* // ── (추가) 스레드‑CPU 바인딩 유지 검사 ──
                int cur_cpu = sched_getcpu();              // 현재 실행 중인 CPU
                if (cur_cpu != cpu) {
                    cpu_set_t re; CPU_ZERO(&re); CPU_SET(cpu, &re);
                    if (pthread_setaffinity_np(pthread_self(),
                                            sizeof(re), &re) == 0) {
                        std::cerr << "[Core " << cpu
                                << "] re‑affinitized (was on CPU "
                                << cur_cpu << ")\n";
                    } else {
                        perror("pthread_setaffinity_np");
                    }
                }
                */

                // std::cout << "==================================" << std::endl;
                if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
                    perror("perf read"); break;
                }
                uint64_t delta = buf[0] - prev;
                prev = buf[0];
                if (delta) { 
                    std::cout << "[Core " << h.cpu << "] Target Time : " << delta << "\n"; 
                    
                    /* ── delta > 0 : delay count 증가 ── */
                    uint64_t delay_ns = static_cast<uint64_t>(delta * speedup); // 시간 * 비율
                    h.local_delay += delay_ns;
                    g_global_delay.fetch_add(delay_ns, std::memory_order_relaxed);
                    
                    uint64_t global = g_global_delay.load(std::memory_order_acquire);
                    // std::cout << "[Core " << h.cpu << "] Updated Global Delay : " << global << std::endl;
                    // std::cout << "[Core " << h.cpu << "] Updated Local Delay : " << h.local_delay << std::endl;
                }

                for (int i = 0; i < 1; ++i) { // 최대 4회 등 안전 상한 -> 그냥 무식하게 1회 
                    uint64_t global = g_global_delay.load(std::memory_order_acquire);
                    uint64_t local  = h.local_delay;

                    if (global <= local) break;
                    // std::cout << "[Core " << h.cpu << "] global : " << global << " local : " << local << std::endl;

                    uint64_t diff = global - local;

                    // 너무 길게 재우지 않도록 cap (예: 200µs)
                    // const uint64_t kCapNs = 200'000; // 필요 시 조정
                    // uint64_t consume = (diff > kCapNs) ? kCapNs : diff;
                    uint64_t consume = diff;
                    
                    // double t = seconds_since_boot();
                    // std::cout << std::fixed << std::setprecision(6); // ← 핵심!
                    // std::cout << "[Core " << h.cpu << "] Delay of " << consume << " ns" << std::endl;

                    timespec ts;
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    uint64_t user_before = ts.tv_sec*1000000000ULL + ts.tv_nsec;

                    uint64_t kernel_done = mcoz_sleep(consume); // 커널에서 ktime_get_ns() 반환

                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    uint64_t user_after = ts.tv_sec*1000000000ULL + ts.tv_nsec;
                   
                    if (h.cpu == 7){
                        std::cout << std::fixed << std::setprecision(6)
                            << "[Core " << h.cpu << "]"
                            << " " << consume << "\n";
                            // << "[" << user_before/1e9 << "] ~"
                            // << "["  << user_after /1e9 << "]\n";
                    }

                    // mcoz_sleep(consume);     // 커널: preempt_disable + ndelay → "코어"에서 실제 소비
                    h.local_delay += consume;
                }

            }

            ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
            close(fd);
        });

        g_handlers.push_back(std::move(h));
        // std::cerr << "[DBG] handler thread for cpu=" << cpu << " launched\n";
    } 

    /* ─── cleanup ─── */
    std::cerr << "[INFO] sampler stopping…\n";
    for (auto& h : g_handlers) {
        if (h.th.joinable()) h.th.join();
    }
    cleanup();
    return 0;
}

/* ───────────── cleanup & sigint ───────────── */
void cleanup(){
    std::cerr << "[INFO] cleanup\n";
    for(auto& v:g_victims) close(v.fd);
    }

void sigint_handler(int){ g_running=false; }
