# coz-go

Go support for the [`coz` causal profiler](https://github.com/plasma-umass/coz).

A traditional profiler tells you *where* your program spends time. Coz tells you
whether optimizing a line would actually make the program faster — which, in
concurrent code, is a different question.

Works on **Linux** and **macOS**.

## Usage

First [install `coz`](https://github.com/plasma-umass/coz#installation), then add
the module:

```
go get github.com/plasma-umass/coz/go
```

Mark the points where your program makes progress. For throughput — "I wish this
happened more often":

```go
import coz "github.com/plasma-umass/coz/go"

for _, req := range requests {
    handle(req)
    coz.Progress()               // equivalent of COZ_PROGRESS
}
```

`coz.ProgressNamed("requests")` is the equivalent of `COZ_PROGRESS_NAMED`.

For latency — "I wish this finished sooner" — mark both ends of the operation:

```go
func handle(req Request) {
    defer coz.Scope("request")()   // COZ_BEGIN now, COZ_END on return
    // ...
}
```

`Scope` fires its end counter even on an early return or a panic. If you need the
two halves apart, `coz.Begin("request")` and `coz.End("request")` are available.

On a hot path you can hoist the counter lookup out of the loop:

```go
requests := coz.NewThroughput("requests")
for _, req := range reqs {
    handle(req)
    requests.Increment()
}
```

`coz.Available()` reports whether the program is running under `coz run`.

## Building and running

Profiling requires **cgo** (`CGO_ENABLED=1`, the default when a C toolchain is
present), because reaching libcoz means calling `dlsym`. With `CGO_ENABLED=0`
this package still builds — every entry point compiles to a no-op — so you can
leave progress points in code that also ships as a static binary.

Coz needs DWARF line tables, and it cannot read the compressed DWARF that Go's
linker emits by default. Build with compression off:

```
go build -ldflags=-compressdwarf=false -o myapp .
coz run --- ./myapp
coz plot --text
```

Without `-compressdwarf=false`, Go emits `.zdebug_*` (ELF) / `__zdebug_*`
(Mach-O) sections and coz will report "Debug information was not found."

If line attribution looks coarse because of inlining, also pass
`-gcflags=all=-l` to disable inlining, or `-gcflags=all='-N -l'` to disable
optimization entirely. Both change the performance profile of the program, so
prefer leaving them off unless you need the extra source fidelity.

## Example

`example/toy.go` runs two goroutines per round; one does twice the work of the
other, so it sits on the critical path.

```
go build -ldflags=-compressdwarf=false -o toy ./example
coz run --- ./toy
coz plot --text
```

```
Source Line                |   Slope |    R² | Max Speedup | Points
---------------------------+---------+-------+-------------+-------
go/example/toy.go:35       |   1.082 |  0.91 | +     29.9% |     4
go/example/toy.go:41       |   0.137 |  1.00 | +      8.9% |     2
```

Line 35 is the loop inside `slowWork` and line 41 the loop inside `fastWork`.
Coz correctly predicts that speeding up `slowWork` speeds up the program roughly
proportionally, while `fastWork` is nearly irrelevant.

## Caveats

**Do not use `runtime/pprof` CPU profiling at the same time.** Coz samples with
`SIGPROF`, which is the same signal Go's own CPU profiler uses; the two cannot
both own it. Calling `pprof.StartCPUProfile` under `coz run` will fight coz for
the signal. Memory, block, and mutex profiles are unaffected.

**Results are per-OS-thread, not per-goroutine.** Coz's virtual speedup works by
delaying threads. Go multiplexes goroutines onto OS threads (`GOMAXPROCS`), so a
progress point tells you about the thread that executed it, and a goroutine that
migrates between threads is not tracked as a unit. In practice this is fine for
CPU-bound work, but a goroutine that is descheduled mid-operation may attribute
some of its delay elsewhere.

**Progress points must be reached often enough.** Coz needs at least a handful of
progress-point visits per experiment (roughly 5), and each experiment runs for
about half a second. A program that reaches its progress point a dozen times
total will produce very few data points.

**On Linux, `coz plot` reports `Runtime: 0.0s` for Go programs.** Go's runtime
exits with a direct `exit_group` syscall rather than libc's `exit`, so coz's
shutdown hook never runs and the final `runtime` record is not written. The
per-experiment records are flushed as they complete, so the profile itself is
intact; only the total-runtime line (used for phase correction) is missing.

## How it works

`coz_shim.c` resolves `_coz_get_counter` and `_coz_add_delays` out of the
injected `libcoz` with `dlsym(RTLD_DEFAULT, ...)`. That means the package does
not need `coz.h` installed at build time, and a binary built against it runs
normally when `libcoz` is absent — every entry point degrades to a no-op.

Incrementing a counter is a plain relaxed atomic add on memory owned by `libcoz`,
performed from Go, so there is no cgo call on the hot path on Linux. On macOS
each progress point additionally calls `_coz_add_delays()`, because macOS has no
per-thread sampling timer and a worker thread only discovers the virtual delay it
owes when it reaches a progress point. This mirrors `_COZ_CHECK_DELAYS` in
`include/coz.h`.
