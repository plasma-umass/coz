#include "perf.h"
#include "timer.h"
#include "log.h"

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
#include <atomic>
#include <signal.h>

static std::vector<int>  g_fds;          // CPU별 카운터 FD
static std::atomic<bool> g_running{true};
static std::atomic<bool> container_live{false};

// gpt 
/* --- cgroup 정보 --- */
struct cg_info {
    std::string path;            // "/sys/fs/cgroup/.../kubepods.slice/pod<uid>"
    int freeze_fd;               // path + "/cgroup.freeze" (O_WRONLY)
};

static std::vector<cg_info> g_victims;            // pause 대상 Pod 목록
static std::atomic<bool>    g_victims_paused{false};
static std::atomic<uint64_t>g_last_activity_ns{0}; // CLOCK_MONOTONIC ns
constexpr uint64_t kIdleWindowNs = 2'000'000;      // 2 ms

enum {
  SampleSignal = SIGPROF,
  SamplePeriod = 1000000,
  SampleBatchSize = 10
};

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

bool is_cgv2 = (access("/sys/fs/cgroup/cgroup.controllers", F_OK) == 0);

// gpt : 해당 경로에 있는 모든 container들
void init_victims(const std::vector<cgroup>& others)
{
    const char* freezer_root_v1 = "/sys/fs/cgroup/freezer";
    const std::string perf_prefix = "/sys/fs/cgroup/perf_event";   // 정확한 접두사

    for (auto& cg : others) {
        std::string path = cg.path;

        /* v1 노드라면 perf_event → freezer 계층으로 접두사 교체 */
        if (!is_cgv2 &&
            path.compare(0, perf_prefix.size(), perf_prefix) == 0)
        {
            path.replace(0, perf_prefix.size(), freezer_root_v1);
        }

        /* freeze 파일 결정 */
        std::string ctrl_file =
            is_cgv2 ? (path + "/cgroup.freeze")
                    : (path + "/freezer.state");

        int fd = open(ctrl_file.c_str(), O_WRONLY);
        if (fd < 0) {
            std::cout << "path | " << ctrl_file << '\n';
            perror("open freeze");
            continue;
        }
        g_victims.push_back({path, fd});
    }
}

inline void freeze(int fd)  {
    std::cout << "freeze!! | " << fd << std::endl;
    if (is_cgv2) write(fd, "1", 1);          // v2
    else         write(fd, "FROZEN", 6);     // v1
}
inline void unfreeze(int fd){
    std::cout << "un-freeze!! | " << fd << std::endl;
    if (is_cgv2) write(fd, "0", 1);          // v2
    else         write(fd, "THAWED", 6);     // v1
}

// gpt : signal handler
static void sigprof_handler(int, siginfo_t*, void*) {
    if (!container_live.load(std::memory_order_relaxed)) {
        container_live.store(true, std::memory_order_relaxed);
        /* 여기서 원하는 액션 수행: 로그, 다른 스레드 깨우기 등 */
    }
    /* 필요하면 read(fd,&val,8) 로 카운터 리셋 */
    uint64_t dummy;
    for (int fd : g_fds){
        read(fd, &dummy, sizeof(dummy));
    }
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
    // pe.disabled       = 1;
    pe.exclude_kernel = 1;
    pe.exclude_idle   = 1;
    // pe.sample_period  = period.count() * 1000000ULL; // ms -> ns
    pe.disabled       = 0;
    pe.sample_period  = 1000;
    pe.sample_type    = 0;
    pe.inherit        = 1;
    pe.wakeup_events  = 10; // 0.5s 마다 링버퍼 비우기
    std::cout << "   >> perf_sampler_sync : attr 설정 완료" << std::endl;


    /* 2. perf_event() */
    auto cpus = online_cpus();
    std::vector<pollfd> pfds;

    g_fds.reserve(cpus.size());
    for (int cpu : cpus) { // 기본 COZ는 -1
        int fd = (int)perf_event_open(&pe,
                                      /*pid      =*/ cg_fd,
                                      /*cpu      =*/ cpu,
                                      /*group_fd =*/ -1,
                                      PERF_FLAG_PID_CGROUP | PERF_FLAG_FD_CLOEXEC);
        if (fd == -1) {
            perror("perf_event_open");
            continue;                      // 실패한 CPU는 건너뜀
        }

        fcntl(fd, F_SETFL,  fcntl(fd, F_GETFL, 0) | O_ASYNC); // 기존 로직 따라해보기
        fcntl(fd, F_SETSIG, SampleSignal);     // SIGPROF
        fcntl(fd, F_SETOWN, getpid());         // 시그널을 받을 PID
        // ioctl(fd, PERF_EVENT_IOC_RESET,  0); 
        // Start counting events
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
        g_fds.push_back(fd);
        pfds.push_back({fd, POLLIN, 0});   // ← poll()용 엔트리 추가
    }

    if (g_fds.empty()) {
        std::cerr << "No perf events opened\n";
        return -1;
    }

    /* 3. SIGPROF 핸들러 등록 -> 대신 poll로 바꾸자.. */
    // struct sigaction sa{};
    // sa.sa_sigaction = sigprof_handler;
    // sa.sa_flags     = SA_SIGINFO;
    // sigaction(SampleSignal, &sa, nullptr);

    // /* 4. timer  */
    // timer t = timer(SampleSignal);

    // /* 5.  start_interval */
    // t.start_interval(SamplePeriod * SampleBatchSize);

    /* 6. main thread 무한 대기 (Ctrl-C로 종료) */
    // while (g_running.load()) {
    //     pause();
    //     if (container_live.load()){
    //         std::cout << "target run : found it !\n" << std::endl;
    //     }
    // } -> poll로 해보기
    while (g_running.load()) {
        int ret = poll(pfds.data(), pfds.size(), -1);   // 무한 대기
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll"); break;
        }

        for (size_t i = 0; i < pfds.size(); ++i) {
            if (pfds[i].revents & POLLIN) {

                /* 1) 타깃 Pod이 방금 CPU를 썼다 = 활동 감지 */
                uint64_t now = std::chrono::duration_cast<
                                std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()
                            ).count();
                g_last_activity_ns.store(now, std::memory_order_relaxed);

                /* 2) 아직 냉동 안 돼 있으면 즉시 pause */
                if (!g_victims_paused.exchange(true)) {
                    for (auto& v : g_victims) freeze(v.freeze_fd);
                    std::cout << "==> victims PAUSED (Pod activity detected)" << std::endl;
                }

                /* 3) 카운터 리셋 */
                uint64_t dummy;
                read(pfds[i].fd, &dummy, sizeof(dummy));
            }

            uint64_t last = g_last_activity_ns.load(std::memory_order_relaxed);
            uint64_t now  = std::chrono::duration_cast<
                            std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()
                            ).count();

            if (g_victims_paused && now - last >= kIdleWindowNs) {
                for (auto& v : g_victims) unfreeze(v.freeze_fd);
                g_victims_paused = false;
                std::cout << "==> victims RESUMED (Pod idle)" << std::endl;
            }
        }

        /* 한 번만 감지하고 끝내고 싶으면:
        if (container_live) break; */
    }

    return 0;
    

    // /* 3. 무한(또는 원하는 시간) 폴링 루프 */
    // std::cout << "Start polling … (Ctrl-C to quit)\n";
    // while (true) {
    //     int ret = poll(pfds.data(), pfds.size(), 1000); // 1 초 타임아웃
    //     if (ret < 0) {
    //         if (errno == EINTR) continue; // 시그널로 깼으면 재시도
    //         perror("poll");
    //         break;
    //     }
    //     if (ret == 0) continue;           // 타임아웃

    //     for (size_t i = 0; i < pfds.size(); ++i) {
    //         if (pfds[i].revents & POLLIN) {
    //             std::cerr << "cgroup activity on CPU "
    //                       << cpus[i] << '\n';
    //             /* 여기서 inject_delay(…) 등 후처리 가능 */
    //             // 읽어서 revents 클리어
    //             char buf[8];
    //             read(pfds[i].fd, buf, sizeof(buf));
    //         }
    //     }
    // }

    // /* 4. 정리 */
    // for (auto& p : pfds) {
    //     ioctl(p.fd, PERF_EVENT_IOC_DISABLE, 0);
    //     close(p.fd);
    // }
    // return 0;
}

void cleanup() {
    if (g_victims_paused)           // 냉동된 채 종료 방지
        for (auto& v : g_victims) unfreeze(v.freeze_fd);

    for (auto& v : g_victims) close(v.freeze_fd);
    for (int fd : g_fds) { ioctl(fd, PERF_EVENT_IOC_DISABLE, 0); close(fd); }
}


void sigint_handler(int) {
    g_running = false;          // perf.cpp 안의 static 변수 사용
}