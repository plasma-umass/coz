//go:build cgo

/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

// Package coz provides Go support for the coz causal profiler.
//
// Coz answers "if I optimize this line, will the program actually get faster?"
// To use it, mark the points where your program makes progress, build with
// uncompressed DWARF, and run the binary under `coz run`:
//
//	go build -ldflags=-compressdwarf=false -o myapp .
//	coz run --- ./myapp
//
// Throughput profiling measures how often a progress point is reached:
//
//	for _, req := range requests {
//	    handle(req)
//	    coz.Progress()
//	}
//
// Latency profiling measures the time between a matched pair of points, via
// Little's law:
//
//	func handle(req Request) {
//	    defer coz.Scope("request")()
//	    // ...
//	}
//
// When the program is not running under `coz run`, libcoz is absent, every
// entry point here is a cheap no-op, and the program behaves normally.
//
// # Interaction with runtime/pprof
//
// Coz samples using SIGPROF, which is the same signal Go's own CPU profiler
// uses. Do not call pprof.StartCPUProfile while profiling under coz; the two
// cannot both own the signal. See the package README for details.
package coz

/*
#cgo linux CFLAGS: -D_GNU_SOURCE
#cgo linux LDFLAGS: -ldl

#include <stdlib.h>
#include "coz_shim.h"
*/
import "C"

import (
	"fmt"
	"runtime"
	"sync"
	"sync/atomic"
	"unsafe"
)

// Counter type constants, matching include/coz.h.
const (
	counterThroughput = C.COZ_SHIM_THROUGHPUT
	counterBegin      = C.COZ_SHIM_BEGIN
	counterEnd        = C.COZ_SHIM_END
)

var initOnce sync.Once

func ensureInit() {
	initOnce.Do(func() { C.coz_shim_init() })
}

// Available reports whether the coz runtime was found, i.e. whether this
// process is running under `coz run`. It is useful in tests and for skipping
// expensive instrumentation when not profiling.
func Available() bool {
	ensureInit()
	return C.coz_shim_available() != 0
}

// A Counter is a handle to one of libcoz's progress-point counters.
//
// Resolving a counter by name costs a cgo call and a lock inside libcoz, so hot
// call sites should hold onto a Counter rather than passing a name on every
// hit. The package-level helpers (Progress, Begin, End) cache Counters for you,
// but that cache costs a map lookup per hit; NewThroughput and friends let you
// skip even that.
//
// The zero Counter is valid and does nothing, which is what you get when the
// program runs without the profiler.
type Counter struct {
	// count aliases the `count` field of libcoz's coz_counter_t, which is a
	// size_t and therefore uintptr-sized. It is nil when not profiling.
	count *uintptr
}

// NewThroughput returns a throughput (COZ_PROGRESS_NAMED) counter.
func NewThroughput(name string) *Counter { return newCounter(counterThroughput, name) }

// NewBegin returns the opening counter of a latency pair (COZ_BEGIN).
func NewBegin(name string) *Counter { return newCounter(counterBegin, name) }

// NewEnd returns the closing counter of a latency pair (COZ_END).
func NewEnd(name string) *Counter { return newCounter(counterEnd, name) }

func newCounter(typ C.int, name string) *Counter {
	ensureInit()

	cname := C.CString(name)
	// libcoz copies the name into its own table, so the C string is ours to
	// free as soon as the call returns.
	defer C.free(unsafe.Pointer(cname))

	ptr := C.coz_shim_get_counter(typ, cname)
	if ptr == nil {
		return &Counter{}
	}
	// `count` is the first field of coz_counter_t, so the struct address is
	// also the address of the counter itself.
	return &Counter{count: (*uintptr)(unsafe.Pointer(ptr))}
}

// Increment records one visit to this progress point.
//
// It is safe to call from multiple goroutines concurrently, and is a no-op on a
// Counter obtained while not profiling.
func (c *Counter) Increment() {
	if c == nil || c.count == nil {
		return
	}
	// Mirrors COZ_INCREMENT_COUNTER in include/coz.h: a relaxed atomic bump of
	// the shared counter, then a check for any virtual delay this thread owes.
	atomic.AddUintptr(c.count, 1)
	checkDelays()
}

// counterCache memoizes name -> *Counter so the package-level helpers avoid a
// cgo call on every progress point.
var counterCache sync.Map // cacheKey -> *Counter

type cacheKey struct {
	typ  C.int
	name string
}

func cachedCounter(typ C.int, name string) *Counter {
	key := cacheKey{typ: typ, name: name}
	if c, ok := counterCache.Load(key); ok {
		return c.(*Counter)
	}
	c := newCounter(typ, name)
	actual, _ := counterCache.LoadOrStore(key, c)
	return actual.(*Counter)
}

// ProgressNamed records a visit to the named throughput progress point.
// It is the equivalent of the COZ_PROGRESS_NAMED macro.
func ProgressNamed(name string) {
	cachedCounter(counterThroughput, name).Increment()
}

// Progress records a visit to a throughput progress point named after the
// calling file and line, the equivalent of the COZ_PROGRESS macro.
//
// Determining the call site costs a stack lookup. Prefer ProgressNamed, or hold
// a Counter from NewThroughput, on very hot paths.
func Progress() {
	pc, _, _, ok := runtime.Caller(1)
	if !ok {
		ProgressNamed("unknown")
		return
	}
	if c, hit := pcCache.Load(pc); hit {
		c.(*Counter).Increment()
		return
	}
	name := callSiteName(pc)
	c := cachedCounter(counterThroughput, name)
	pcCache.Store(pc, c)
	c.Increment()
}

// pcCache memoizes caller PC -> *Counter for Progress.
var pcCache sync.Map // uintptr -> *Counter

// callSiteName renders a program counter as "file:line", matching the naming
// that COZ_PROGRESS produces from __FILE__ and __LINE__.
func callSiteName(pc uintptr) string {
	fn := runtime.FuncForPC(pc)
	if fn == nil {
		return "unknown"
	}
	// pc is the return address; step back into the call instruction so the
	// reported line is the coz.Progress() call rather than the line after it.
	file, line := fn.FileLine(pc - 1)
	return fmt.Sprintf("%s:%d", file, line)
}

// Begin marks the start of a latency-profiled operation, the equivalent of the
// COZ_BEGIN macro. It must be paired with an End of the same name.
func Begin(name string) {
	cachedCounter(counterBegin, name).Increment()
}

// End marks the completion of a latency-profiled operation, the equivalent of
// the COZ_END macro. It must be paired with a Begin of the same name.
func End(name string) {
	cachedCounter(counterEnd, name).Increment()
}

// Scope marks the start of a latency-profiled operation and returns a function
// that marks its end, so the pair survives early returns and panics:
//
//	func handle(req Request) {
//	    defer coz.Scope("request")()
//	    // ...
//	}
func Scope(name string) func() {
	begin := cachedCounter(counterBegin, name)
	end := cachedCounter(counterEnd, name)
	begin.Increment()
	return end.Increment
}
