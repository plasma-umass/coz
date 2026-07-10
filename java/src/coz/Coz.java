/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

package coz;

/**
 * Progress points for the coz causal profiler.
 *
 * <p>Coz answers "if I optimize this line, will the program actually get faster?" Mark the points
 * where your program makes progress, then run it under the coz JVMTI agent:
 *
 * <pre>
 *   javac -g -d classes MyApp.java
 *   java -agentpath:/path/to/libcozjava.so=output=profile.jsonl,scope=com.example \
 *        -cp classes:coz.jar com.example.MyApp
 *   coz plot --text -i profile.jsonl
 * </pre>
 *
 * <p>When the agent is not loaded every method here is a no-op, so instrumented code runs
 * normally outside profiling.
 *
 * <p>All methods are safe to call from any thread.
 */
public final class Coz {

    /**
     * Whether the coz agent is attached.
     *
     * <p>Deliberately in a holder class rather than a static field of {@code Coz}. The agent binds
     * the natives below with RegisterNatives during VM init, and to find this class it calls JNI
     * FindClass -- which *initializes* it. A {@code static final boolean ENABLED = probe()} on
     * {@code Coz} would therefore run before the natives were bound, see UnsatisfiedLinkError, and
     * latch "not profiling" for the life of the VM.
     *
     * <p>The holder is only initialized on first use from application code, which is necessarily
     * after VM init, so by then the natives are bound.
     */
    private static final class Enabled {
        static final boolean VALUE = probe();

        private static boolean probe() {
            try {
                return available0();
            } catch (UnsatisfiedLinkError e) {
                return false;
            }
        }
    }

    private Coz() {}

    /** Whether this JVM is running under the coz agent. */
    public static boolean isAvailable() {
        return Enabled.VALUE;
    }

    /**
     * Records a visit to the named throughput progress point: "I wish this happened more often."
     * The equivalent of {@code COZ_PROGRESS_NAMED}.
     */
    public static void progress(String name) {
        if (Enabled.VALUE) {
            progress0(name);
        }
    }

    /**
     * Records a visit to a throughput progress point named after the calling class, method and
     * line. The equivalent of {@code COZ_PROGRESS}.
     *
     * <p>Determining the call site walks one stack frame. On a hot path, prefer
     * {@link #progress(String)}.
     */
    public static void progress() {
        if (Enabled.VALUE) {
            progress0(callSite());
        }
    }

    /**
     * Marks the start of a latency-profiled operation: "I wish this finished sooner." The
     * equivalent of {@code COZ_BEGIN}. Must be paired with {@link #end(String)}.
     */
    public static void begin(String name) {
        if (Enabled.VALUE) {
            begin0(name);
        }
    }

    /**
     * Marks the completion of a latency-profiled operation, the equivalent of {@code COZ_END}.
     * Must be paired with {@link #begin(String)}.
     */
    public static void end(String name) {
        if (Enabled.VALUE) {
            end0(name);
        }
    }

    /**
     * Runs {@code body} bracketed by {@link #begin(String)} and {@link #end(String)}, so the end
     * counter fires even if the body throws.
     *
     * <pre>
     *   Coz.scope("request", () -&gt; handle(request));
     * </pre>
     */
    public static void scope(String name, Runnable body) {
        begin(name);
        try {
            body.run();
        } finally {
            end(name);
        }
    }

    /** Renders the caller of {@code progress()} as "Class.method:line". */
    private static String callSite() {
        return StackWalker.getInstance()
                .walk(frames -> frames
                        .skip(2) // callSite, progress
                        .findFirst()
                        .map(f -> f.getClassName() + "." + f.getMethodName() + ":" + f.getLineNumber())
                        .orElse("unknown"));
    }

    // Bound by the agent's RegisterNatives at VM init.
    private static native boolean available0();

    private static native void progress0(String name);

    private static native void begin0(String name);

    private static native void end0(String name);
}
