# coz-swift

Swift support for the [`coz` causal profiler](https://github.com/plasma-umass/coz).

A traditional profiler tells you *where* your program spends time. Coz tells you
whether optimizing a line would actually make the program faster — which, in
concurrent code, is a different question.

Works on **Linux** and **macOS**.

## Usage

First [install `coz`](https://github.com/plasma-umass/coz#installation), then add
the package:

```swift
.package(url: "https://github.com/plasma-umass/coz.git", from: "0.3.0")
```

and depend on the `Coz` product:

```swift
.target(name: "MyApp", dependencies: [.product(name: "Coz", package: "coz")])
```

Mark the points where your program makes progress. For throughput — "I wish this
happened more often":

```swift
import Coz

for request in requests {
    handle(request)
    Coz.progress()                  // equivalent of COZ_PROGRESS
}
```

`Coz.progress("requests")` is the equivalent of `COZ_PROGRESS_NAMED`.

For latency — "I wish this finished sooner" — bracket the operation:

```swift
try Coz.scope("request") {
    try handle(request)
}
```

`scope` fires its end counter even if the body throws. If you need the halves
apart, `Coz.begin("request")` and `Coz.end("request")` are available.

On a hot path, hold a counter instead of looking it up by name each time:

```swift
let requests = Coz.Counter(throughput: "requests")
for request in batch {
    handle(request)
    requests.increment()
}
```

`Coz.isAvailable` reports whether the program is running under `coz run`.

## Building and running

Coz needs DWARF line tables, so build with debug information:

```
swift build -c release -Xswiftc -g
coz run --- .build/release/MyApp
coz plot --text
```

A plain `swift build` (debug) also works and carries debug info by default.

## Example

`Sources/CozToy` runs two threads per round; one does twice the work of the
other, so it sits on the critical path.

```
swift build -c release -Xswiftc -g
coz run --- .build/release/CozToy
coz plot --text
```

```
Source Line                              |   Slope |    R² | Max Speedup | Points
-----------------------------------------+---------+-------+-------------+-------
swift/Sources/CozToy/main.swift:44       |   0.616 |  0.90 | +     46.9% |     9
swift/Sources/CozToy/main.swift:55       |   0.052 |  1.00 | +      4.7% |     2
```

Line 44 is inside `slowWork`'s loop and line 55 inside `fastWork`'s. Coz predicts
that speeding up `slowWork` speeds up the program roughly proportionally, and
finds `fastWork` nearly irrelevant.

## Caveats

Both of these come down to the same thing: **coz only understands threads and
blocking primitives that it interposes.** It interposes `pthread_create`,
`pthread_join`, pthread mutexes and condition variables. Everything below follows
from that.

**On macOS, coz cannot delay libdispatch's global queues.** Coz applies a virtual
speedup by *delaying every other thread*, and it can only delay threads it knows
about. On Darwin the global `DispatchQueue` workers are kernel-created
pthread-workqueue threads that never call `pthread_create`. Coz will still
*sample* them, so their lines appear in the profile, but the virtual speedup has
nothing to slow down and every line comes out with a slope near zero.

If your hot work runs on `DispatchQueue.global()` and every line reads as flat,
that is why. Run the code you want to profile on a `Thread` or a plain `pthread`,
as `Sources/CozToy` does. This does not affect Linux, where
swift-corelibs-libdispatch creates its workers with `pthread_create`.

**Blocking on a primitive coz cannot see will skew your results.** A thread
blocked on a raw futex, a Mach semaphore, or a `DispatchSemaphore` still gets
charged for the virtual delays it "should" have paid while it was asleep. If the
thread that hits your progress point is the one doing the waiting, every line
comes out with a *negative* slope. (This is not hypothetical — this example
originally joined its threads with a `DispatchSemaphore` and reported exactly
that.)

Either block on something coz interposes — `pthread_join`, a pthread mutex or
condvar — or tell coz about the primitive yourself:

```swift
Coz.preBlock()
semaphore.wait()
Coz.postBlock(skipDelays: true)   // true: another thread woke us

Coz.catchUp()                     // pay delays before we may wake someone
semaphore.signal()
```

These map to `COZ_PRE_BLOCK`, `COZ_POST_BLOCK` and `COZ_CATCH_UP` in `coz.h`.

**Don't use a SIGPROF-based profiler at the same time.** Coz samples with
`SIGPROF` and will fight anything else that wants it.

**Progress points must be reached often enough.** Coz needs roughly five
progress-point visits per experiment, and each experiment runs for about half a
second. A program that reaches its progress point a dozen times in total will
produce very few data points.

**Swift optimizes counted loops aggressively.** A microbenchmark like
`for i in 0..<n { acc &+= i }` gets folded to a closed form and deleted, leaving
coz nothing to sample. This bites synthetic examples, not real programs.

## How it works

`Sources/CCoz/coz_shim.c` resolves `_coz_get_counter` and `_coz_add_delays` out
of the injected `libcoz` with `dlsym(RTLD_DEFAULT, ...)`. That means the package
needs neither `coz.h` at build time nor a system `coz-profiler` package, and a
binary built against it runs normally when `libcoz` is absent — every entry point
degrades to a no-op.

Counter lookups are memoized in C, so a hot progress point does not pay for
libcoz's lock and name table on every hit, and the Swift side holds no global
mutable state (the package builds clean under Swift 6 strict concurrency).

Incrementing is a relaxed atomic add. On macOS each progress point additionally
calls `_coz_add_delays()`, because macOS has no per-thread sampling timer and a
worker thread only discovers the virtual delay it owes when it reaches a progress
point. This mirrors `_COZ_CHECK_DELAYS` in `include/coz.h`.
