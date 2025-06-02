package main

import (
    "fmt"
    "os"
    "syscall"
    "time"
    "unsafe"
    "path/filepath"
	"log"
)

// perf_event_attr 구조체 (커널 헤더 기준)
type PerfEventAttr112 struct {
    Type               uint32
    Size               uint32
    Config             uint64
    Sample_type        uint64
    Sample_period      uint64
    Sample_freq        uint64
    Wakeup_events      uint32
    Wakeup_watermark   uint32
    BP_type            uint32
    Reserved1          uint32
    BP_addr            uint64
    BP_len             uint64
    Branch_sample_type uint64
    Sample_regs_user   uint64
    Sample_stack_user  uint32
    Clockid            int32
    Sample_regs_intr   uint64
    Aux_watermark      uint32
    Reserved2          uint32
    Flags              uint64
} // 총 112바이트 (커널 기준)

// perf constants
const (
    PERF_TYPE_SOFTWARE        = 1
    PERF_COUNT_SW_TASK_CLOCK  = 1 // ✅ 여기로 변경
    PERF_FLAG_PID_CGROUP      = 1 << 3
    PERF_FLAG_FD_CLOEXEC      = 1 << 3
    PERF_ATTR_FLAG_DISABLED       = 1 << 0
    PERF_ATTR_FLAG_ENABLE_ON_EXEC = 1 << 7
)

// syscall wrapper
func perfEventOpen(attr *PerfEventAttr112, pid, cpu, groupFd int, flags uintptr) (int, error) {
    r0, _, e1 := syscall.Syscall6(syscall.SYS_PERF_EVENT_OPEN,
        uintptr(unsafe.Pointer(attr)), uintptr(pid), uintptr(cpu), uintptr(groupFd), flags, 0)
    if e1 != 0 {
        return -1, e1
    }
    return int(r0), nil
}

// 🔥 샘플링 이벤트 발생 시 → delay 주입
func perfSamplerSync(cgFd int, period time.Duration, delta float64, others []*cgroup, mode string) {
	
	// 이 버전은 그냥 "PERF_COUNT_SW_TASK_CLOCK"을 읽어서 누적 시간만 보고 있음
	attr := &PerfEventAttr112{
		Type:   PERF_TYPE_SOFTWARE,
		Config: PERF_COUNT_SW_TASK_CLOCK,
		Flags:  PERF_ATTR_FLAG_DISABLED,
	}
	attr.Size = uint32(unsafe.Sizeof(*attr)) // 이제 괜찮음, 정확히 112
	
	// 정확한 크기만 전달
	// safeSize := uint32(unsafe.Offsetof(attr.Flags) + unsafe.Sizeof(attr.Flags))
	// log.Printf("safeSize for attr: %d", safeSize)
	attr.Size = 112

	log.Printf("PerfEventAttr112: %+v\n", attr)

	// cpu := 0 // 또는 다른 특정 CPU
	fd, err := perfEventOpen((*PerfEventAttr112)(unsafe.Pointer(attr)), -1, 0, -1, PERF_FLAG_PID_CGROUP|PERF_FLAG_FD_CLOEXEC)
	if err != nil {
		fmt.Fprintf(os.Stderr, "perf open failed: %v\n", err)
		return
	}
	defer syscall.Close(fd)

	attr := &PerfEventAttr112{
		Type:          0, // PERF_TYPE_HARDWARE
		Config:        0, // PERF_COUNT_HW_INSTRUCTIONS
		Sample_type:   1, // PERF_SAMPLE_IP
		Sample_period: 100000, // 샘플 간격 (100k instructions 마다)
		Flags:         PERF_ATTR_FLAG_DISABLED,
	}
	attr.Size = 112

	log.Printf("PerfEventAttr112: %+v\n", attr)
	fd, err := perfEventOpen(attr, -1, 0, -1, 0)
	

	// 이벤트 초기화 및 시작
	syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), 0x2400, 0) // RESET
	syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), 0x2401, 0) // ENABLE

	fds := []syscall.PollFd{{Fd: int32(fd), Events: syscall.POLLIN}}

	for {
		_, err := syscall.Poll(fds, 1000)
		if err != nil {
			log.Printf("poll error: %v", err)
			continue
		}

		if fds[0].Revents&syscall.POLLIN != 0 {
			log.Printf("🎯 컨테이너에서 작업 시작 감지됨!")
			// delay 주기 또는 freeze 등 처리
		}
	}
}