// perf.cpp – signal-mode with verbose debug
#include "perf.h"

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <signal.h>
#include <linux/perf_event.h>
#include <chrono>
#include <vector>
#include <atomic>
#include <iostream>
#include <cstring>
#include <sys/time.h>

/* ──────────────── global state ──────────────── */
static std::vector<int>  g_fds;                 // CPU별 perf FD
static std::atomic<bool> g_running   {true};
static std::atomic<bool> g_paused    {false};
static std::atomic<uint64_t> g_lastNs{0};       // 마지막 활동 시각(ns)
// static int kSigRT = -1;                         // SIGNAL 초기 dummy 값
static struct sigaction sa_alrm;                // freeze-unfreeze alarm용

struct cg_info { std::string path; int fd; };
static std::vector<cg_info> g_victims;
static bool is_cgv2 = (access("/sys/fs/cgroup/cgroup.controllers",F_OK)==0);

static long perf_event_open(struct perf_event_attr* attr,int pid,int cpu,int grp,unsigned long flags){
    long r=syscall(__NR_perf_event_open,attr,pid,cpu,grp,flags);
    if(r==-1) perror("perf_event_open");
    else      std::cerr << "[DBG] perf_event_open ok cpu="<<cpu<<" fd="<<r<<"\n";
    return r;
}

static std::vector<int> online_cpus(){
    int n=sysconf(_SC_NPROCESSORS_ONLN); std::vector<int> v; for(int i=0;i<n;++i) v.push_back(i);
    std::cerr << "[DBG] online cpu cnt="<<v.size()<<"\n"; return v; }



/* ───────────── helper: 현재 상태 문자열 얻기 ───────────── */
static std::string read_cg_state(const std::string& cg_path)
{
    std::string ctrl = is_cgv2 ? cg_path + "/cgroup.freeze"
                               : cg_path + "/freezer.state";
    char buf[32] = {};
    int fd = open(ctrl.c_str(), O_RDONLY);
    if (fd < 0) { perror("open state"); return "?"; }
    ssize_t n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n < 0) { perror("read state"); return "?"; }
    // v1 은 "FROZEN\n"·"THAWED\n", v2 는 "0\n"·"1\n"
    if (is_cgv2) return (buf[0] == '1') ? "FROZEN" : "THAWED";
    return std::string(buf, strcspn(buf, "\n"));   // 줄바꿈 제거
}


/* ───────────── helper: fd → path 매핑 찾아 로그 ───────── */
static void log_state_by_fd(int fd)
{
    for (const auto& v : g_victims)
        if (v.fd == fd) {
            std::cerr << "   [DBG] state now = "
                      << read_cg_state(v.path) << '\n';
            return;
        }
}


/* ───────────── util: freeze / unfreeze ───────────── */
inline void freeze(int fd){
    std::cerr << "     >> freeze fd=" << fd << '\n';
    if (is_cgv2) write(fd, "1", 1); else write(fd, "FROZEN", 6);
    // log_state_by_fd(fd);                  // ★ 상태 표시
}

inline void unfreeze(int fd){
    std::cerr << "     >> unfreeze fd=" << fd << '\n';
    if (is_cgv2) write(fd, "0", 1); else write(fd, "THAWED", 6);
    // log_state_by_fd(fd);                  // ★ 상태 표시
}

inline void freezer(int fd, int delay_ns)
{
    std::thread([fd, delay_ns]() {
        using namespace std::chrono;

        /* 1) freeze */
        if (is_cgv2) write(fd, "1", 1);
        else         write(fd, "FROZEN", 6);

        /* 2) 기준 시각(t0) 찍고 잠들기 */
        auto t0 = steady_clock::now();
        std::this_thread::sleep_for(nanoseconds(delay_ns));

        /* 3) unfreeze */
        if (is_cgv2) write(fd, "0", 1);
        else         write(fd, "THAWED", 6);

        /* 4) 깨어난 직후(t1) = unfreeze 호출 직후 */
        auto t1 = steady_clock::now();
        auto actual_ns = duration_cast<nanoseconds>(t1 - t0).count();

        /* 5) 로그 한 번에 출력 */
        std::ostringstream oss;
        oss << "fd = " << fd
            << " (slept "    << static_cast<double>(actual_ns) / 1'000'000 << " ms"
            << ", expected " << static_cast<double>(delay_ns) / 1'000'000 << " ms"
            << ", gap " << (static_cast<double>(actual_ns) - static_cast<double>(delay_ns)) / 1'000'000 << " ms)"
            << '\n';
        std::cout << oss.str();
    }).detach();
}

inline void start_freezer(int delay){
    if (delay !=0) {
        for (auto& v : g_victims) freezer(v.fd, delay);
    }
}


/* ─────────────── signal handler ─────────────── */
// unfreeze용 SIGALRM 핸들러
static void sigalrm_handler(int) {
    for (auto& v : g_victims) unfreeze(v.fd);
    g_paused.store(false, std::memory_order_relaxed);
}

// 초기화 시점 (perf_sampler_sync 시작부나 main() 직후에)
void setup_unfreeze_timer() {
    // 1) SIGALRM 핸들러 등록
    sa_alrm = {};
    sa_alrm.sa_handler = sigalrm_handler;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = 0;
    sigaction(SIGALRM, &sa_alrm, nullptr);
}

static void sig_handler(int, siginfo_t* info, void*){
    int fd=info? info->si_fd : -1;
    uint64_t val; if(fd>=0) read(fd,&val,8);

    char buf[64];
    int n = snprintf(buf, sizeof(buf), "[SIG] Signal from %d\n", fd);
    write(STDERR_FILENO, buf, n);

    uint64_t now=std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    g_lastNs.store(now,std::memory_order_relaxed);

    /* 로직변경
    if(!g_paused.exchange(true)){
        // std::cerr << "[ACT] victims pause\n";
        for(auto& v:g_victims) freeze(v.fd);
    }
    if(fd>=0) ioctl(fd,PERF_EVENT_IOC_REFRESH,1);
    */

    // 1. freeze
    if (!g_paused.exchange(true)) {
        for (auto& v : g_victims) freeze(v.fd);
    }

    // 2. 다음 overflow를 위해 다시 무장
    if (fd >= 0) ioctl(fd, PERF_EVENT_IOC_REFRESH, 1);

    // 3) 0.1 ms 뒤에 unfreeze 용 SIGALRM 예약
    itimerval tv{};
    tv.it_value.tv_sec  = 0;
    tv.it_value.tv_usec = 100;   // 100 µs = 0.1 ms
    setitimer(ITIMER_REAL, &tv, nullptr);

}


/* ───────────── sampler entry ───────────── */
int perf_sampler_sync(int cg_fd,std::chrono::milliseconds /*period*/,double /*delta*/,
                      const std::vector<cgroup>& others,const std::string& /*mode*/){
    std::cerr << "[INFO] sampler start\n";

    setup_unfreeze_timer();

    /* SIGPROF 언블록: 함수 맨 첫 줄에 추가 */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    pthread_sigmask(SIG_UNBLOCK, &set, nullptr);

    /* victims 초기화 */
    const char* root_v1="/sys/fs/cgroup/freezer";
    const std::string perf_prefix="/sys/fs/cgroup/perf_event";
    for(auto& cg:others){
        std::string path=cg.path;
        if(!is_cgv2 && path.rfind(perf_prefix,0)==0)
            path.replace(0,perf_prefix.size(),root_v1);
        std::string ctrl=is_cgv2?path+"/cgroup.freeze":path+"/freezer.state";
        int fd=open(ctrl.c_str(),O_WRONLY);
        if(fd<0){perror("open freeze");continue;}
        g_victims.push_back({path,fd});
        std::cerr << "[DBG] victim added: fd="<<fd<<"\n";
    }

    // polling 구현
    perf_event_attr pe{}; pe.size=sizeof(pe);
    pe.type            = PERF_TYPE_SOFTWARE;
    pe.config          = PERF_COUNT_SW_TASK_CLOCK;
    pe.sample_period   = 0;                            // 샘플링 이벤트 없음 -> event_based가 아님
    pe.disabled        = 1;
    pe.read_format     = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    pe.exclude_idle    = 1;

    // if (kSigRT == -1) kSigRT = SIGRTMIN + 3;

    auto cpus=online_cpus();
    for(int cpu:cpus){
        int fd=perf_event_open(&pe,cg_fd,cpu,-1,PERF_FLAG_PID_CGROUP|PERF_FLAG_FD_CLOEXEC);
        if(fd<0) continue;

        if (fcntl(fd, F_SETOWN, getpid()) == -1) perror("F_SETOWN");
        // if (fcntl(fd, F_SETSIG, kSigRT) == -1) perror("F_SETSIG"); 
        int fl = fcntl(fd, F_GETFL,0);
        if (fcntl(fd, F_SETFL, fl | O_NONBLOCK | O_ASYNC) == -1) perror("F_SETFL");


        // ioctl(fd,PERF_EVENT_IOC_REFRESH,1); 
        ioctl(fd,PERF_EVENT_IOC_ENABLE,0);
        ioctl(fd,PERF_EVENT_IOC_RESET, 0 );
        
        g_fds.push_back(fd);
        
        // std::cerr << "[DBG] set O_ASYNC owner="<<gettid()
        //   <<" fcntl="<<std::hex<<fcntl(fd,F_GETFL)<<std::dec<<"\n";
  
    }
    if(g_fds.empty()) return -1;


    /* polling 구현 */
    // 이전 값 저장 
    std::vector<uint64_t> prev(g_fds.size(), 0);
    uint64_t sum = 0;

    // 10ms 폴링 루프
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::cout << "==================================" << std::endl;
        for (size_t i = 0; i < g_fds.size(); ++i) {
            uint64_t buf[3] = {};
            if (read(g_fds[i], buf, sizeof(buf)) != sizeof(buf)) {
                perror("perf read");
                continue;
            }

            uint64_t delta = buf[0] - prev[i];
            prev[i] = buf[0];
            sum += delta;

            // std::cout << "[FD " << g_fds[i] << "]" << "task clock per 1s : " << delta << std::endl;
        //   // delta만큼의 context-switch 발생
        //   if (/* freeze_mode && */ delta > threshold) {
        //     freeze(cg_fd);
        //   } else {
        //     unfreeze(cg_fd);
        //   }
        }
        std::cout << "raw sum : " << sum << " ns" << std::endl;
        // uint64_t : 나노초를 넘김
        start_freezer(sum);
        sum = 0;
    }

    cleanup();
    
    return 0;
}


/* ───────────── cleanup & sigint ───────────── */
void cleanup(){
    std::cerr << "[INFO] cleanup\n";
    if(g_paused) for(auto& v:g_victims) unfreeze(v.fd);
    for(auto& v:g_victims) close(v.fd);
    for(int fd:g_fds){ ioctl(fd,PERF_EVENT_IOC_DISABLE,0); close(fd);} }

void sigint_handler(int){ g_running=false; }
