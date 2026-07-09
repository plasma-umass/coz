/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

import XCTest

#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

@testable import Coz

final class CozTests: XCTestCase {
    // Without libcoz injected, every entry point must be an inert no-op rather
    // than a null dereference. This is the normal `swift test` case.
    func testEntryPointsAreSafeWithoutProfiler() throws {
        try XCTSkipIf(Coz.isAvailable, "running under `coz run`; this covers the un-profiled path")

        Coz.progress()
        Coz.progress("named")
        Coz.begin("op")
        Coz.end("op")
        Coz.Counter(throughput: "t").increment()
        Coz.Counter(begin: "b").increment()
        Coz.Counter(end: "e").increment()
    }

    func testScopeReturnsBodyValue() {
        let result = Coz.scope("scoped") { 42 }
        XCTAssertEqual(result, 42)
    }

    struct Boom: Error {}

    func testScopeEndsEvenWhenBodyThrows() {
        XCTAssertThrowsError(try Coz.scope("throwing") { throw Boom() }) { error in
            XCTAssertTrue(error is Boom)
        }
    }

    // The C-side cache must hand back the same counter for the same name, and
    // must not conflate names across counter types.
    func testCountersAreCachedPerNameAndType() {
        let a = Coz.Counter(throughput: "dup")
        let b = Coz.Counter(throughput: "dup")
        XCTAssertEqual(a, b)

        // Same name, different type: distinct counters (both nil when not
        // profiling, so this only asserts the lookup does not crash).
        _ = Coz.Counter(begin: "dup")
        _ = Coz.Counter(end: "dup")
    }

    // Exercises the pthread_once and the cache mutex from many threads.
    //
    // Uses raw pthreads rather than DispatchQueue.concurrentPerform: on Linux,
    // libdispatch leaves the XCTest runner wedged after a concurrentPerform,
    // and the *next* test in the suite never starts.
    func testConcurrentIncrementsAreSafe() {
        let threadCount = 16
        var handles: [pthread_t] = []

        for _ in 0..<threadCount {
            #if canImport(Darwin)
            var handle: pthread_t?
            let rc = pthread_create(&handle, nil, { _ in
                for _ in 0..<200 {
                    Coz.progress("concurrent")
                    Coz.scope("concurrent-scope") {}
                }
                return nil
            }, nil)
            XCTAssertEqual(rc, 0)
            if let handle { handles.append(handle) }
            #else
            var handle = pthread_t()
            let rc = pthread_create(&handle, nil, { _ in
                for _ in 0..<200 {
                    Coz.progress("concurrent")
                    Coz.scope("concurrent-scope") {}
                }
                return nil
            }, nil)
            XCTAssertEqual(rc, 0)
            handles.append(handle)
            #endif
        }

        for handle in handles {
            pthread_join(handle, nil)
        }
    }
}
