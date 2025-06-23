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

    std::string cmd = "kubectl get pod " + pod + " -n " + ns + " -o jsonpath={.status.containerStatuses[0].containerID}";
    FILE* fp = popen(cmd.c_str(), "r");
    if(!fp) throw std::runtime_error("kubectl failed");
    char buf[256];
    std::string cid;
    if(fgets(buf, sizeof(buf), fp)) cid = buf;
    pclose(fp);
    cid.erase(cid.find_last_not_of(" \n\r\t")+1);
    const std::string prefix = "containerd://";
    if(cid.rfind(prefix,0)==0) cid = cid.substr(prefix.size());

    std::string root = "/sys/fs/cgroup/unified/kubepods.slice";
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
    std::cerr << "found cgroup id: " << cg.id << std::endl;
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

static std::vector<cgroup> discover_other_pods(const cgroup& tgt, const std::string& exclude) {
    (void)tgt; (void)exclude; // placeholder
    return {};
}

int main(int argc, char** argv) {
    const char* target_pod = nullptr;
    const char* freeze_mode = "freezer";
    double speedup = 0.25;
    int period_ms = 5;

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
        cgroup tgt = resolve_target_cgroup(target_pod);
        int fd = open(tgt.path.c_str(), O_DIRECTORY);
        if(fd < 0) {
            perror("open cgroup");
            return 1;
        }
        auto others = discover_other_pods(tgt, "");
        perf_sampler_sync(fd, std::chrono::milliseconds(period_ms), speedup, others, freeze_mode);
    } catch(const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

