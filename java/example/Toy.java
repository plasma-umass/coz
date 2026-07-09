/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

package example;

import coz.Coz;

/**
 * The Java port of benchmarks/toy: two threads do unequal amounts of work, and a progress point
 * marks each completed round.
 *
 * <p>slowWork does twice the work of fastWork, so it sits on the critical path. A correct causal
 * profile predicts a large speedup for the loop inside slowWork and almost none for the one inside
 * fastWork.
 */
public final class Toy {

    /**
     * An {@code int} counter, not a {@code long}, on purpose.
     *
     * <p>HotSpot strip-mines int counted loops and leaves a safepoint poll in the outer strip, so
     * the VM can reach a safepoint mid-loop. A long counted loop gets no poll here, and the coz
     * agent's GetAllStackTraces then blocks until the loop *finishes* -- time-to-safepoint of ~76ms,
     * which collapses the sample rate and pins every sample to the line after the loop.
     */
    private static final int ITERATIONS = 60_000_000;

    /** Written by the workers so the JIT cannot delete their loops. */
    static volatile long slowSink;
    static volatile long fastSink;

    private Toy() {}

    private static long xorshift(long value) {
        value ^= value << 13;
        value ^= value >>> 7;
        value ^= value << 17;
        return value;
    }

    static void slowWork() {
        long acc = 0x9E3779B97F4A7C15L;
        for (int i = 0; i < ITERATIONS; i++) {
            acc = xorshift(acc);
        }
        slowSink = acc;
    }

    static void fastWork() {
        long acc = 0x9E3779B97F4A7C15L;
        for (int i = 0; i < ITERATIONS / 2; i++) {
            acc = xorshift(acc);
        }
        fastSink = acc;
    }

    public static void main(String[] args) throws InterruptedException {
        System.out.println("Starting. coz agent attached: " + Coz.isAvailable());

        for (int round = 0; round < 100; round++) {
            Thread slow = new Thread(Toy::slowWork, "slow");
            Thread fast = new Thread(Toy::fastWork, "fast");
            slow.start();
            fast.start();
            slow.join();
            fast.join();

            // One round of work finished: this is the throughput progress point.
            Coz.progress("round");

            System.out.print(".");
            System.out.flush();
        }

        System.out.println("\nDone. " + slowSink + " " + fastSink);
    }
}
