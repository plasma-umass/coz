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
}

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
func perfEventOpen(attr *PerfEventAttr, pid, cpu, groupFd int, flags uintptr) (int, error) {
    r0, _, e1 := syscall.Syscall6(syscall.SYS_PERF_EVENT_OPEN,
        uintptr(unsafe.Pointer(attr)), uintptr(pid), uintptr(cpu), uintptr(groupFd), flags, 0)
    if e1 != 0 {
        return -1, e1
    }
    return int(r0), nil
}

// 🔥 샘플링 이벤트 발생 시 → delay 주입
func perfSamplerSync(cgFd int, period time.Duration, delta float64, others []*cgroup, mode string) {
	attr := &PerfEventAttr{
		Type:   PERF_TYPE_SOFTWARE,
		Config: PERF_COUNT_SW_TASK_CLOCK,
		Flags:  PERF_ATTR_FLAG_DISABLED,
	}
	attr.Size = uint32(unsafe.Offsetof(attr.Flags) + unsafe.Sizeof(attr.Flags))

	log.Printf("perfeventattr: %+v\n", attr)

	cpu := 0 // 또는 다른 특정 CPU
	fd, err := perfEventOpen(attr, -1, cpu, -1, PERF_FLAG_PID_CGROUP|PERF_FLAG_FD_CLOEXEC)
	if err != nil {
		fmt.Fprintf(os.Stderr, "perf open failed: %v\n", err)
		return
	}
	defer syscall.Close(fd)

	// 이벤트 초기화 및 시작
	if _, _, err := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), 0x2400 /* PERF_EVENT_IOC_RESET */, 0); err != 0 {
		log.Printf("ioctl reset failed: %v\n", err)
	}
	if _, _, err := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), 0x2401 /* PERF_EVENT_IOC_ENABLE */, 0); err != 0 {
		log.Printf("ioctl enable failed: %v\n", err)
	}

	buf := make([]byte, 8)

	for {
		_, err := syscall.Read(fd, buf)
		if err != nil {
			log.Printf("read error: %v", err)
			time.Sleep(time.Millisecond * 100) // fail-safe
			continue
		}

		usec := time.Duration(delta * float64(period.Nanoseconds()))

		for _, cg := range others {
			switch mode {
			case "freezer":
				_ = os.WriteFile(filepath.Join(cg.Path, "cgroup.freeze"), []byte{'1'}, 0644)
			case "cpu-weight":
				_ = os.WriteFile(filepath.Join(cg.Path, "cpu.weight"), []byte("1"), 0644)
			}
		}

		time.Sleep(usec)

		for _, cg := range others {
			switch mode {
			case "freezer":
				_ = os.WriteFile(filepath.Join(cg.Path, "cgroup.freeze"), []byte{'0'}, 0644)
			case "cpu-weight":
				_ = os.WriteFile(filepath.Join(cg.Path, "cpu.weight"), []byte("100"), 0644)
			}
		}
	}
}
