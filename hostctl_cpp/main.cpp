#include "perf.h"
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>   // signal(), SIGINT  ← C++ 헤더
#include <cstdlib>   // atexit()
#include <algorithm>   // std::find, std::all_of ...
#include <sstream>     // std::istringstream  (discover_other_pods에서 사용)

/* 0. kubeconfig 경로 확보*/
const char* kc = getenv("KUBECONFIG");      // 환경변수 읽기
std::string kc_arg = kc ? (" --kubeconfig " + std::string(kc)) : "";

static bool is_system_ns(const std::string& ns)
{
    /* 필요 시 목록 추가 */
    static const std::vector<std::string> sys = {
        "kube-system", "kube-public", "kube-node-lease"
    };
    return std::find(sys.begin(), sys.end(), ns) != sys.end();
}

static uint64_t get_cgroup_id(const std::string& path) {
    struct stat st;
    if(stat(path.c_str(), &st) != 0) return 0;
    return st.st_ino;
}

static cgroup resolve_target_cgroup(const std::string& target_pod) {
    size_t slash = target_pod.find('/');
    if(slash == std::string::npos) {
        throw std::runtime_error("invalid pod format namespace/pod");
    }
    std::string ns = target_pod.substr(0, slash);
    std::string pod = target_pod.substr(slash+1);

    std::string cmd = "kubectl get pod " + pod + " -n " + ns + kc_arg + " -o jsonpath={.status.containerStatuses[0].containerID}";
    FILE* fp = popen(cmd.c_str(), "r");
    if(!fp) throw std::runtime_error("kubectl failed");
    char buf[256];
    std::string cid;
    if(fgets(buf, sizeof(buf), fp)) cid = buf;
    pclose(fp);
    cid.erase(cid.find_last_not_of(" \n\r\t")+1);
    const std::string prefix = "containerd://";
    if(cid.rfind(prefix,0)==0) cid = cid.substr(prefix.size());

    // 중요
    // cgroup mount가 어떻게 되어있느냐에 따라 다름
    // 만약 /sys/fs/cgroup/ 하위에 perf_event가 있다면 거기로, 아니라면 unified로 
    const char* base = (access("/sys/fs/cgroup/perf_event", F_OK) == 0)
                        ? "/sys/fs/cgroup/perf_event"
                        : "/sys/fs/cgroup/unified";

    std::string root = std::string(base) + "/kubepods.slice";
    std::string needle = "cri-containerd-" + cid + ".scope";
    std::string found;
    for(const auto& dir : std::filesystem::recursive_directory_iterator(root)) {
        if(dir.is_directory() && dir.path().filename() == needle) {
            found = dir.path();
            break;
        }
    }
    if(found.empty()) {
        throw std::runtime_error("cgroup path not found for cid="+cid);
    }
    cgroup cg{found, get_cgroup_id(found)};
    std::cout << "found cgroup id: " << cg.id << std::endl;
    return cg;
}

static void inject_delay(const std::vector<cgroup>& others, uint64_t usec, const std::string& mode) {
    for(const auto& cg : others) {
        if(mode == "freezer") {
            std::ofstream(cg.path + "/cgroup.freeze") << '1';
            std::this_thread::sleep_for(std::chrono::microseconds(usec));
            std::ofstream(cg.path + "/cgroup.freeze") << '0';
        } else if(mode == "cpu-weight") {
            std::ofstream(cg.path + "/cpu.weight") << "1";
            std::this_thread::sleep_for(std::chrono::microseconds(usec));
            std::ofstream(cg.path + "/cpu.weight") << "100";
        }
    }
}

// select victims
static std::vector<cgroup>
discover_other_pods(const cgroup& tgt, const std::string& exclude /* 예: ns/pod */)
{
    std::vector<cgroup> out;
    std::cout << "In >> discover_other_pods\n";

    // DEBUG : 오잉 이거 empty다
    std::cout << "target : " << exclude << std::endl;

    /* 1. kubectl 로 전체 Pod 목록 추출 */
    std::string cmd =
        "kubectl get pods --all-namespaces" +
        kc_arg +
        " -o "
        "jsonpath='{range .items[*]}{.metadata.namespace} "
        "{.metadata.name} "
        "{.status.containerStatuses[0].containerID}{\"\\n\"}{end}'";
    FILE* fp = popen(cmd.c_str(), "r");   // ← .c_str() 중요
    if (!fp) { perror("kubectl"); return out; }

    /* 2. cgroup mount 루트 확정 */
    const char* base = (access("/sys/fs/cgroup/perf_event", F_OK) == 0)
                       ? "/sys/fs/cgroup/perf_event"
                       : "/sys/fs/cgroup/unified";
    const std::string root = std::string(base) + "/kubepods.slice";

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        std::string ns, pod, cid;
        std::istringstream iss(line);
        iss >> ns >> pod >> cid;
        cid.erase(cid.find_last_not_of(" \n\r\t") + 1);

        /* 2-1. system NS·타깃·exclude 필터 */
        if (is_system_ns(ns))             continue;
        if (!exclude.empty() && ns + "/" + pod == exclude) {
            // std::cout << "[SKIP] " << ns << "/" << pod
            //           << ", Target excluded" << std::endl;
            continue;
        }
        /* 2-2. containerd:// prefix 제거 */
        const std::string pref = "containerd://";
        if (cid.rfind(pref, 0) == 0) cid = cid.substr(pref.size());

        /* 2-3. cgroup 경로 탐색 (cri-containerd-<cid>.scope) */
        const std::string needle = "cri-containerd-" + cid + ".scope";
        std::string found;
        for (const auto& dir :
             std::filesystem::recursive_directory_iterator(root))
        {
            if (dir.is_directory() &&
                dir.path().filename() == needle)
            {
                found = dir.path(); break;
            }
        }
        if (found.empty() || found == tgt.path) {
            // std::cout << "[SKIP] Pod=" << ns << "/" << pod
            //     << ": No Cgroup" << std::endl;
            continue; // 타깃 제외
        }

        // DEBUG: VICTIM으로 선정된 pod 정보
        // std::cout << "[VICTIM] Pod=" << ns << "/" << pod
        //           << ", cgroup path=" << found << std::endl;

        out.push_back({found, get_cgroup_id(found)});
    }
    pclose(fp);

    std::cout << "discover_other_pods: " << out.size() << " victims\n";
    std::cout << "Out >> discover_other_pods\n";
    return out;
}

int main(int argc, char** argv) {
    printf("In >> main\n");
    std::cerr << "[DBG] my pid=" << getpid() << " tid=" << gettid() << '\n';

    const char* target_pod = nullptr;
    const char* freeze_mode = "freezer";
    double speedup = 0.25;
    int period_ms = 5;

    signal(SIGINT, sigint_handler);  // Ctrl-C 누르면 poll 루프 탈출
    atexit(cleanup);                 // 어떤 경로든 종료 시 해동 보장   

    static struct option opts[] = {
        {"target-pod", required_argument, 0, 't'},
        {"period", required_argument, 0, 'p'},
        {"speedup", required_argument, 0, 's'},
        {"freeze-mode", required_argument, 0, 'f'},
        {0,0,0,0}
    };

    int opt;
    while((opt = getopt_long(argc, argv, "t:p:s:f:", opts, nullptr)) != -1) {
        switch(opt) {
        case 't': target_pod = optarg; break;
        case 'p': period_ms = atoi(optarg); break;
        case 's': speedup = atof(optarg); break;
        case 'f': freeze_mode = optarg; break;
        }
    }

    if(!target_pod) {
        std::cerr << "need --target-pod" << std::endl;
        return 1;
    }

    try {
        // 1. target pod의 cgroup을 찾기
        cgroup tgt = resolve_target_cgroup(target_pod);
        std::cout << "target cgroup path : " << tgt.path << std::endl;
        int cg_fd = open(tgt.path.c_str(), O_DIRECTORY);
        if(cg_fd < 0) {
            perror("open cgroup");
            return 1;
        }
        std::cout << "cg_fd : " << cg_fd << std::endl;
        // 2. 다른 pod들을 관리하는 아이 만들기
        auto others = discover_other_pods(tgt, target_pod);
        // 3. perf_sampler_sync 적용 -> perf event open!!
        // begin_sampling()을 그대로 따라해보자
        perf_sampler_sync(cg_fd, std::chrono::milliseconds(period_ms), speedup, others, freeze_mode);
        
    } catch(const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
