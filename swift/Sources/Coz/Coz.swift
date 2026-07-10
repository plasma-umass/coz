/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

import CCoz

/// Swift support for the coz causal profiler.
///
/// Coz answers "if I optimize this line, will the program actually get faster?"
/// Mark the points where your program makes progress, build with debug info,
/// and run the binary under `coz run`:
///
/// ```
/// swift build -c release -Xswiftc -g
/// coz run --- .build/release/MyApp
/// ```
///
/// When the program is not running under `coz run` the libcoz runtime is absent
/// and every entry point here is a no-op.
public enum Coz {
    /// Whether the coz runtime was found, i.e. whether this process is running
    /// under `coz run`.
    public static var isAvailable: Bool {
        coz_shim_available() != 0
    }

    /// A handle to one of libcoz's progress-point counters.
    ///
    /// The convenience functions below look a counter up by name on each call,
    /// which takes a lock. On a hot path, hold a `Counter` instead:
    ///
    /// ```swift
    /// let requests = Coz.Counter(throughput: "requests")
    /// for request in requests { handle(request); requests.increment() }
    /// ```
    ///
    /// A `Counter` obtained while not profiling is inert.
    // @unchecked because a raw pointer is not Sendable on its face. libcoz owns
    // this memory, allocates it once, and keeps it alive for the life of the
    // process; the only mutation is the relaxed atomic increment inside
    // coz_shim_hit. So sharing a Counter across threads is safe.
    public struct Counter: @unchecked Sendable, Equatable {
        private let counter: UnsafeMutableRawPointer?

        private init(type: Int32, name: String) {
            let ptr = name.withCString { coz_shim_cached_counter(type, $0) }
            self.counter = UnsafeMutableRawPointer(ptr)
        }

        /// A throughput counter, the equivalent of `COZ_PROGRESS_NAMED`.
        public init(throughput name: String) {
            self.init(type: Int32(COZ_SHIM_THROUGHPUT), name: name)
        }

        /// The opening counter of a latency pair, the equivalent of `COZ_BEGIN`.
        public init(begin name: String) {
            self.init(type: Int32(COZ_SHIM_BEGIN), name: name)
        }

        /// The closing counter of a latency pair, the equivalent of `COZ_END`.
        public init(end name: String) {
            self.init(type: Int32(COZ_SHIM_END), name: name)
        }

        /// Records one visit to this progress point. Safe to call concurrently.
        public func increment() {
            coz_shim_hit(counter?.assumingMemoryBound(to: coz_counter_t.self))
        }
    }

    /// Records a visit to a throughput progress point, the equivalent of
    /// `COZ_PROGRESS`.
    ///
    /// The point is named after the call site unless `name` is given.
    public static func progress(
        _ name: String? = nil,
        file: String = #fileID,
        line: Int = #line
    ) {
        Counter(throughput: name ?? "\(file):\(line)").increment()
    }

    /// Marks the start of a latency-profiled operation, the equivalent of
    /// `COZ_BEGIN`. Must be paired with `end(_:)` of the same name.
    public static func begin(_ name: String) {
        Counter(begin: name).increment()
    }

    /// Marks the completion of a latency-profiled operation, the equivalent of
    /// `COZ_END`. Must be paired with `begin(_:)` of the same name.
    public static func end(_ name: String) {
        Counter(end: name).increment()
    }

    /// Runs `body` bracketed by `begin(name)` and `end(name)`.
    ///
    /// The end counter fires even if `body` throws.
    ///
    /// ```swift
    /// try Coz.scope("request") { try handle(request) }
    /// ```
    public static func scope<T>(_ name: String, _ body: () throws -> T) rethrows -> T {
        begin(name)
        defer { end(name) }
        return try body()
    }

    // MARK: - Custom synchronization
    //
    // Coz interposes pthread mutexes, condition variables and joins, so it knows
    // when a thread blocks and does not charge it for delays it could not have
    // paid. It cannot see primitives it does not interpose -- raw futexes, Mach
    // semaphores, `DispatchSemaphore` -- and a thread blocked on one of those
    // will be charged anyway, skewing every measurement it takes part in.
    //
    // These mirror COZ_PRE_BLOCK / COZ_POST_BLOCK / COZ_CATCH_UP in coz.h and
    // let you tell coz about such a primitive:
    //
    //     Coz.preBlock()
    //     semaphore.wait()
    //     Coz.postBlock(skipDelays: true)
    //
    //     Coz.catchUp()          // before a signal that may unblock someone
    //     semaphore.signal()

    /// Call immediately before blocking on a primitive coz does not interpose.
    public static func preBlock() {
        coz_shim_pre_block()
    }

    /// Call immediately after such a block returns.
    ///
    /// Pass `skipDelays: true` when the wake-up came from another thread (the
    /// usual case: a semaphore was signalled, a lock was handed over), and
    /// `false` when it may have been spurious or a timeout.
    public static func postBlock(skipDelays: Bool) {
        coz_shim_post_block(skipDelays ? 1 : 0)
    }

    /// Pay any outstanding virtual delay before doing something that might
    /// unblock another thread.
    public static func catchUp() {
        coz_shim_catch_up()
    }
}
