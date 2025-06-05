package main

import (
    "log"
    "time"
    "unsafe"
    "golang.org/x/sys/unix"
	"syscall"
	"runtime"
)


// perf_event_attr 구조체 (커널 헤더 기준)
type PerfEventAttr struct {
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
	// 필요한 hw 이벤트 id
    PERF_TYPE_HARDWARE        = 0
    PERF_COUNT_HW_INSTRUCTIONS = 0
    PERF_SAMPLE_IP = 1 << 0
	// 필요한 flag
    PERF_FLAG_PID_CGROUP      = 1 << 2
    PERF_FLAG_FD_CLOEXEC      = 1 << 3

    PERF_ATTR_FLAG_DISABLED       = 1 << 0
    PERF_ATTR_FLAG_ENABLE_ON_EXEC = 1 << 7
)

// syscall wrapper
func perfEventOpen(attr *PerfEventAttr, pid, cpu, groupFd int, flags uintptr) (int, error) {
    r0, _, e1 := syscall.Syscall6(syscall.SYS_PERF_EVENT_OPEN,
        uintptr(unsafe.Pointer(attr)), uintptr(pid), uintptr(cpu), uintptr(groupFd), flags, 0)
    if e1 != 0 {
        return -1, e1
    }
    return int(r0), nil
}

func onlineCPUs() []int {
	n := runtime.NumCPU()
	cpus := make([]int, n)
	for i := 0; i<n; i++ {
		cpus[i] = i
	}

	log.Printf("# CPU = %d", len(cpus))
	return cpus
}

// 🔥 샘플링 이벤트 발생 시 → delay 주입
func perfSamplerSync(cgFd int, period time.Duration, delta float64, others []*cgroup, mode string) {
	attr := &PerfEventAttr{
		Type:          PERF_TYPE_HARDWARE,
		Config:        PERF_COUNT_HW_INSTRUCTIONS,
		Sample_period: 1000, // 100k instructions마다 샘플링
		Sample_type:   PERF_SAMPLE_IP,
		Flags:         PERF_ATTR_FLAG_DISABLED,
		Wakeup_events: 1,
	}
	attr.Size = 112

    log.Printf("PerfEventAttr: %+v\n", attr)

	var pollFds []unix.PollFd

	// ******************************************************************** //
	for _, cpu := range onlineCPUs() {
		fd, err := perfEventOpen(attr, cgFd, cpu, -1, PERF_FLAG_PID_CGROUP|PERF_FLAG_FD_CLOEXEC)
		if err != nil {
			log.Fatalf("[%d] perfEventOpen failed: %v", cpu, err)
		}
		defer unix.Close(fd)

		// 이벤트 초기화 및 시작
		unix.IoctlSetInt(fd, unix.PERF_EVENT_IOC_RESET, 0)
		unix.IoctlSetInt(fd, unix.PERF_EVENT_IOC_ENABLE, 0)

		// fds := []unix.PollFd{{Fd: int32(fd), Events: unix.POLLIN}}
		pollFds = append(pollFds, unix.PollFd{Fd: int32(fd), Events: unix.POLLIN})
	}    

	for {
		_, err := unix.Poll(pollFds, 1000)
		if err != nil {
			log.Printf("poll error: %v", err)
			continue
		}

		for i, pfd := range pollFds {
			if pfd.Revents&unix.POLLIN != 0 {
				log.Printf("cpu %d → 컨테이너 작업 감지!", i)
				// To-do : 후처리 작업 연결
			}
		}
	}
}