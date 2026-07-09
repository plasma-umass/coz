//go:build cgo

package coz

import (
	"runtime"
	"strings"
	"sync"
	"testing"
)

// When libcoz is absent (the normal `go test` case) every entry point must be a
// safe no-op rather than a nil dereference.
func TestNoProfilerIsNoOp(t *testing.T) {
	if Available() {
		t.Skip("running under `coz run`; this test covers the un-profiled path")
	}

	Progress()
	ProgressNamed("named")
	Begin("op")
	End("op")
	Scope("scoped")()

	NewThroughput("t").Increment()
	NewBegin("b").Increment()
	NewEnd("e").Increment()
}

func TestNilCounterIncrementIsSafe(t *testing.T) {
	var c *Counter
	c.Increment()            // nil receiver
	(&Counter{}).Increment() // zero value, no underlying libcoz counter
}

// Counters are memoized, so the same name must yield the same handle.
func TestCounterCacheReturnsSameInstance(t *testing.T) {
	a := cachedCounter(counterThroughput, "dup")
	b := cachedCounter(counterThroughput, "dup")
	if a != b {
		t.Errorf("cachedCounter returned distinct handles for the same name")
	}

	// Same name, different counter type, must not collide.
	if got := cachedCounter(counterBegin, "dup"); got == a {
		t.Errorf("throughput and begin counters collided in the cache")
	}
}

func TestCallSiteNameIsFileAndLine(t *testing.T) {
	pc, _, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("runtime.Caller(0) failed")
	}
	name := callSiteName(pc)
	if !strings.Contains(name, "coz_test.go:") {
		t.Errorf("callSiteName(%#x) = %q, want it to mention coz_test.go:<line>", pc, name)
	}
}

func TestConcurrentIncrementsDoNotRace(t *testing.T) {
	// Exercised under `go test -race`; without libcoz these are no-ops, but the
	// cache and sync.Once paths are still hit from many goroutines.
	var wg sync.WaitGroup
	for i := 0; i < 32; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				ProgressNamed("concurrent")
				Scope("concurrent-scope")()
			}
		}()
	}
	wg.Wait()
}
