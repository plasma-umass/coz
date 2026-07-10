/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

// The Swift port of benchmarks/toy: two concurrent tasks do unequal amounts of
// work, and a progress point marks each completed round.
//
// slowWork does twice the work of fastWork, so it sits on the critical path. A
// correct causal profile predicts a large speedup for the loop inside slowWork
// and almost none for the one inside fastWork.
//
//     swift build -c release -Xswiftc -g
//     coz run --- .build/release/CozToy
//     coz plot --text

import Coz

#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

let iterations: UInt64 = 120_000_000

/// Keeps the optimizer from deleting the loops that feed it.
@inline(never)
func blackHole(_ value: UInt64) {
    if value == UInt64.max { print("unreachable") }
}

// An xorshift step, rather than a summation: LLVM's scalar evolution can fold
// `acc += i` over a counted loop into a closed form and delete the loop
// entirely, which leaves coz nothing to sample. Each function keeps its own
// copy of the loop (@inline(never)) so the two get distinct source lines.

@inline(never) @Sendable
func slowWork() {
    var acc: UInt64 = 0x9E37_79B9_7F4A_7C15
    for _ in 0..<iterations {
        acc ^= acc << 13
        acc ^= acc >> 7
        acc ^= acc << 17
    }
    blackHole(acc)
}

@inline(never) @Sendable
func fastWork() {
    var acc: UInt64 = 0x9E37_79B9_7F4A_7C15
    for _ in 0..<(iterations / 2) {
        acc ^= acc << 13
        acc ^= acc >> 7
        acc ^= acc << 17
    }
    blackHole(acc)
}

/// A retained box so the closure survives the trip through pthread's void*.
private final class ThreadBox {
    let body: @Sendable () -> Void
    init(_ body: @escaping @Sendable () -> Void) { self.body = body }
}

/// Starts `body` on a new thread and returns its handle.
///
/// Deliberately a raw pthread rather than `Thread` + `DispatchSemaphore`. Coz
/// interposes `pthread_create` and `pthread_join`, so it knows when this thread
/// starts and when the main thread blocks waiting for it. A `DispatchSemaphore`
/// is a futex that coz cannot see, so the main thread -- the one that hits the
/// progress point -- would be charged for virtual delays while it was blocked,
/// and every line would come out with a negative slope.
func spawn(_ body: @escaping @Sendable () -> Void) -> pthread_t {
    let arg = Unmanaged.passRetained(ThreadBox(body)).toOpaque()

    #if canImport(Darwin)
    var handle: pthread_t?
    let rc = pthread_create(&handle, nil, { arg in
        Unmanaged<ThreadBox>.fromOpaque(arg).takeRetainedValue().body()
        return nil
    }, arg)
    precondition(rc == 0, "pthread_create failed")
    return handle!
    #else
    var handle = pthread_t()
    let rc = pthread_create(&handle, nil, { arg in
        Unmanaged<ThreadBox>.fromOpaque(arg!).takeRetainedValue().body()
        return nil
    }, arg)
    precondition(rc == 0, "pthread_create failed")
    return handle
    #endif
}

print("Starting.")

for _ in 0..<100 {
    let slow = spawn(slowWork)
    let fast = spawn(fastWork)
    pthread_join(slow, nil)
    pthread_join(fast, nil)

    // One round of work finished: this is the throughput progress point.
    Coz.progress()

    print(".", terminator: "")
    fflush(nil)  // not stdout: Glibc declares it as a mutable global, which Swift 6 rejects
}

print("\nDone.")
