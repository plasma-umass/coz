// main.go – Coz-Container hostctl 데몬 진입점
package main

import (
    "bytes"            // UDS로 받은 바이트를 숫자로 변환
    "encoding/binary" // LittleEndian 변환
    "flag"            // CLI 플래그 파싱
    "fmt"             // 에러 메시지 포맷팅
    "log"             // 로그 출력
    "net"             // Unix Datagram 소켓
    "os"              // 파일 및 권한 조작
    "path/filepath"   // cgroup 파일 경로 조합
    "time"            // 시간·Sleep
)

// CLI 플래그 정의
var (
    targetPod  = flag.String("target-pod", "", "namespace/pod to profile (필수)")
    targetCID  = flag.String("target-cid", "", "container ID to profile (옵션)")
    period     = flag.Duration("period", 5*time.Millisecond, "샘플링 주기 P (예: 5ms)")
    speedup    = flag.Float64("speedup", 0.25, "가상 속도향상 Δ (0<Δ<1)")
    freezeMode = flag.String("freeze-mode", "freezer", "지연 주입 모드: freezer | cpu-weight")
    excludeNS  = flag.String("exclude", "kube-system,istio-system", "제외할 네임스페이스 목록(콤마 구분)")
    sockPath   = "/run/coz_hostctl.sock" // UDS 경로
)

// cgroup 정보를 담는 구조체
// path: 해당 Pod 또는 컨테이너의 cgroup 디렉터리 경로
type cgroup struct{ path string }

func main() {
    // 1) 플래그 파싱 → flag 변수에 값이 채워짐
    flag.Parse()

    // 2) 인자 검증: target-pod 또는 target-cid 중 하나는 필수
    if *targetPod == "" && *targetCID == "" {
        log.Fatalf("error: specify --target-pod or --target-cid")
    }

    // 3) 대상 cgroup 경로 해결
    tgt, err := resolveTargetCgroup(*targetPod, *targetCID)
    if err != nil {
        log.Fatalf("resolve target cgroup: %v", err)
    }
    log.Printf("[hostctl] target cgroup: %s", tgt.path)

    // 4) 기존 UDS 소켓 제거 후 새로 생성
    os.RemoveAll(sockPath)
    listener, err := net.ListenUnixgram("unixgram", &net.UnixAddr{Name: sockPath, Net: "unixgram"})
    if err != nil {
        log.Fatalf("failed to listen on %s: %v", sockPath, err)
    }
    defer listener.Close()
    // 모든 사용자가 쓸 수 있도록 권한 변경
    os.Chmod(sockPath, 0666)

    // 5) 지연 토큰 수신용 버퍼(8바이트)와 리스트 갱신용 ticker 준비
    delayBuf := make([]byte, 8)
    ticker := time.NewTicker(10 * time.Second)
    // 초기 Other Pod cgroup 목록 생성 (타깃 및 시스템 제외)
    others := discoverOtherPods(tgt, *excludeNS)

    // 6) 메인 이벤트 루프
    for {
        select {
        case <-ticker.C:
            // 주기적으로 Other Pod 목록을 갱신
            others = discoverOtherPods(tgt, *excludeNS)
        default:
            // UDS로부터 delay_us 메시지 수신 대기
            n, _, err := listener.ReadFrom(delayBuf)
            if err != nil || n != 8 {
                // 잘못된 패킷은 무시하고 루프 재진입
                continue
            }
            // 바이트 배열을 uint64(usec)로 변환
            usec := binary.LittleEndian.Uint64(bytes.TrimSpace(delayBuf))
            // 받은 usec 값만큼 Other Pod에 지연 주입
            injectDelay(others, usec, *freezeMode)
        }
    }
}