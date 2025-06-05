package main

import (
    "bytes"
    "encoding/binary"
    "flag"
    "fmt"
    "log"
    "os"
    "path/filepath"
    "time"
	"os/exec"
	"strings"
	"syscall"
)

// --- CLI flags ---
var (
    targetPod  = flag.String("target-pod", "", "namespace/pod to profile")
    period     = flag.Duration("period", 5*time.Millisecond, "sampling period P")
    speedup    = flag.Float64("speedup", 0.25, "virtual speedup Δ (0<Δ<1)")
    freezeMode = flag.String("freeze-mode", "freezer", "freezer | cpu-weight")
    excludeNS  = flag.String("exclude", "kube-system,istio-system", "namespaces to exclude from freezing")
    sockPath   = "/run/coz_hostctl.sock"
)

// simple struct to hold cgroup meta
type cgroup struct {
    Path string
    ID   uint64
}

func main() {
	// 파싱해서 targetPod 받음
	// --target-pod <NS/POD>
    flag.Parse()
    if *targetPod  == "" {
        log.Fatalf("need --target-pod")
    }

    // 1) resolve target cgroup (placeholder) 
	// Pod 이름만 넣어서 받기 - 완료
    tgt, err := resolveTargetCgroup(*targetPod)
    if err != nil {
        log.Fatalf("resolve target: %v", err)
    }
    log.Printf("target cgroup: %s", tgt.Path)

	// cgroup 경로에서 FD 열기
	file, err := os.Open(tgt.Path)
	if err != nil {
		log.Fatalf("cgroup FD 열기 실패: %v", err)
	}
    
	cgFd := int(file.Fd())
	log.Printf("cgroup FD : %d", cgFd)
	
	// 타겟 제외 나머지 pod cgroup 추출
    others := discoverOtherPods(tgt, *excludeNS)

    // perf 샘플러 실행 (blocking)
    log.Printf("perfSamplerSync 호출 : perfSamplerSync(%d, %d, %f, %d, %s)", cgFd, *period, *speedup, others, *freezeMode)
    perfSamplerSync(cgFd, *period, *speedup, others, *freezeMode)
}

// 주소로부터 unode id를 추출 : 완
func getCgroupIDFromPath(path string) (uint64, error) {
    fi, err := os.Stat(path)
    if err != nil {
        return 0, fmt.Errorf("cgroup 경로 stat 실패: %w", err)
    }
    st, ok := fi.Sys().(*syscall.Stat_t)
    if !ok {
        return 0, fmt.Errorf("stat 변환 실패")
    }
    return st.Ino, nil
}

// pod 이름으로부터 cgroup directory 경로 추출 : 완
func resolveTargetCgroup(target_pod string) (*cgroup, error) {
	// 1. ns - pod 분리
    parts := strings.SplitN(target_pod, "/", 2)
    if len(parts) != 2 {
        return nil, fmt.Errorf("pod 형식이 올바르지 않습니다 (namespace/podName)")
    }
    ns, pod := parts[0], parts[1]

	// 2. cid 추출
    out, err := exec.Command(
        "kubectl", "get", "pod", pod, "-n", ns,
        "-o", "jsonpath={.status.containerStatuses[0].containerID}",
    ).Output()
    if err != nil {
        return nil, fmt.Errorf("kubectl 실행 실패: %v", err)
    }
	
    cid := strings.TrimSpace(string(out))
    cid = strings.TrimPrefix(cid, "containerd://")

    // log.Printf("target cid: %s", cid) 성공

	// 3. cid 바탕으로 경로 구성
	cgroupRoot := "/sys/fs/cgroup/unified/kubepods.slice"

	// 4. 트리 탐색 : cri-containerd-<cid>.scope 디렉토리 찾기
    var foundPath string
    filepath.Walk(cgroupRoot, func(path string, info os.FileInfo, err error) error {
        if err != nil {
            return err
        }
        if info.IsDir() && info.Name() == "cri-containerd-"+cid+".scope" {
            foundPath = path
            return filepath.SkipDir
        }
        return nil
    })
    if foundPath == "" {
        return nil, fmt.Errorf("cgroup 경로를 찾을 수 없습니다: cid=%s", cid)
    }

	// 5. inode 조회
	id, err := getCgroupIDFromPath(foundPath)
	if err != nil {
		return nil, err
	}
	log.Printf("찾아낸 cgroup id : %d", id)
	
	return &cgroup{Path: foundPath, ID: id}, nil
}

// 다른 pod들 찾기
func discoverOtherPods(tgt *cgroup, exclude string) []*cgroup {
    // Placeholder: return empty slice for now
    return []*cgroup{}
}

func bytesToUint64(b []byte) uint64 {
    return binary.LittleEndian.Uint64(bytes.TrimSpace(b))
}

// dealy 주입
func injectDelay(others []*cgroup, usec uint64, mode string) {
    for _, cg := range others {
        switch mode {
        case "freezer":
            f := filepath.Join(cg.Path, "cgroup.freeze")
            os.WriteFile(f, []byte{'1'}, 0644)
            time.Sleep(time.Duration(usec) * time.Microsecond)
            os.WriteFile(f, []byte{'0'}, 0644)
        case "cpu-weight":
            w := filepath.Join(cg.Path, "cpu.weight")
            os.WriteFile(w, []byte("1"), 0644)
            time.Sleep(time.Duration(usec) * time.Microsecond)
            os.WriteFile(w, []byte("100"), 0644)
        }
    }
}