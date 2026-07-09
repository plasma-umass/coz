# coz-java

Java support for the [`coz` causal profiler](https://github.com/plasma-umass/coz),
as a JVMTI agent.

A traditional profiler tells you *where* your program spends time. Coz tells you
whether optimizing a line would actually make the program faster — which, in
concurrent code, is a different question.

Works on **Linux** and **macOS**.

## Why this is an agent and not a binding

The Rust, Go and Swift bindings are thin: they hand their progress points to
`libcoz` and let it do the work. `libcoz` samples the program counter, maps it to
a source line through DWARF, and inserts virtual delays by pausing native threads.

None of that survives contact with the JVM. Java methods are compiled at run time
into anonymous memory with no DWARF, so a sampled PC maps to nothing, and the
threads to delay are Java threads `libcoz` has never heard of. So this agent
reimplements the causal profiling loop in JVM terms:

- **Sampling.** Every sample period it asks JVMTI for the top frame of every Java
  thread (`GetAllStackTraces`) and resolves it to a source line with
  `GetLineNumberTable`. [JCoz](https://github.com/Decave/JCoz) instead used
  `AsyncGetCallTrace`, a private, undocumented HotSpot entry point.
  `GetAllStackTraces` is specified, stable, and works on every JDK and both
  platforms — at the cost of the safepoint caveat below.
- **Virtual speedup.** To pretend one line runs faster, the agent slows
  everything else down: for each sample on the selected line it owes every *other*
  thread a pause of `speedup × sample_period`, paid with
  `SuspendThreadList`/`ResumeThreadList`, batched so the VM reaches a safepoint
  once per batch rather than once per sample.
- **Progress points.** `coz.Coz.progress()` and friends bump a counter; the delta
  across an experiment is the throughput measurement.

Output is coz's own JSON Lines profile, so `coz plot` reads it unchanged.

## Building

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_JAVA_AGENT=ON
cmake --build build --target cozjava coz-jar
```

This produces `build/java/libcozjava.so` (the agent; also `.so` on macOS, since
the JVM does not care about the extension) and `build/java/coz.jar` (the API).

## Usage

Compile with `-g` so the class files carry the line tables the agent reads back:

```
javac -g -cp build/java/coz.jar -d classes MyApp.java
```

Mark the points where your program makes progress:

```java
import coz.Coz;

for (Request request : requests) {
    handle(request);
    Coz.progress("requests");     // equivalent of COZ_PROGRESS_NAMED
}
```

`Coz.progress()` with no argument names the point after the call site. For
latency, `Coz.begin("request")` / `Coz.end("request")`, or
`Coz.scope("request", () -> handle(request))`, which ends even if the body throws.

Then run under the agent and plot:

```
java -agentpath:build/java/libcozjava.so=output=profile.jsonl,scope=com.example \
     -cp classes:build/java/coz.jar com.example.MyApp

coz plot --text -i profile.jsonl
```

Agent options, comma-separated after `=`:

| option | default | meaning |
|---|---|---|
| `output` | `profile.jsonl` | where to write the profile |
| `scope` | application classes | only profile this package prefix (`com.example`) |
| `period` | `5000000` | nanoseconds between samples |
| `verbose` | off | log startup, sample count, mean time-to-safepoint |

Without the agent, every `Coz` method is a no-op, so instrumented code runs
normally in production.

## Example

`example/Toy.java` runs two threads per round; `slowWork` does twice the work of
`fastWork`, so it sits on the critical path.

```
Source Line         |   Slope |    R² | Max Speedup | Points
--------------------+---------+-------+-------------+-------
example/Toy.java:47 |   0.496 |  1.00 | +     41.3% |     3
example/Toy.java:55 |   0.090 |  0.21 | +     14.1% |     9
```

Line 47 is `slowWork`'s loop and line 55 is `fastWork`'s. Coz predicts that
speeding up `slowWork` speeds up the program roughly proportionally, and finds
`fastWork` nearly irrelevant.

## Caveats

**Do not profile on GraalVM's JIT.** The agent samples at safepoints, so its
sample rate is bounded by the VM's time-to-safepoint. Under HotSpot's C2 that is
about **100µs**. Under the Graal JIT it was about **100ms** on the toy above —
a thousand times worse, because Graal left no safepoint poll in the hot counted
loop. The agent then samples ~10 times a second and cannot attribute anything.

Run on a HotSpot JDK, or on GraalVM pass `-XX:-UseJVMCICompiler` to fall back to
C2. `verbose=1` prints the mean time-to-safepoint so you can check:

```
[coz-java] wrote profile.jsonl (1671 sample ticks, mean GetAllStackTraces 147 us)
```

If that number is in the milliseconds, your profile is not trustworthy.

**Sampling is safepoint-biased.** A sample lands on the last safepoint poll the
thread passed, not on the instruction it was executing. In a hot loop with a poll
per strip, that is close enough to attribute the loop. In a loop with no poll at
all, every sample lands on the line *after* the loop. This is the price of using
documented JVMTI instead of `AsyncGetCallTrace`; it is the same reason most
Java sampling profilers report method-level rather than line-level truth.

**Compile with `-g`.** Without line tables the agent cannot map a bytecode index
to a line, and the class is silently skipped. `javac -g` is enough.

**Use `scope=`.** Without it, the agent profiles everything that is not the JDK,
which for a large application means resolving line tables for a lot of classes it
will never select.

**`coz plot` charts throughput points only.** `begin`/`end` counters are recorded
in the profile as `latency-point` records, but the plot reader currently consumes
only `throughput-point`.

**Suspending threads costs.** Virtual speedup is implemented by actually pausing
threads, so a profiled run is slower than an unprofiled one. On the toy the
overhead is roughly 10%.

## JDK support

The agent uses only documented JVMTI 1.2: `GetAllStackTraces`,
`GetLineNumberTable`, `GetSourceFileName`, `SuspendThreadList`,
`ResumeThreadList`, `RunAgentThread`, `RegisterNatives`. It needs no `tools.jar`
(removed in JDK 9), no `AsyncGetCallTrace`, and no dynamic agent attach, so
[JEP 451](https://openjdk.org/jeps/451) does not apply — it loads statically with
`-agentpath`.

Verified on JDK 17 (Temurin, macOS arm64) and JDK 21 (Ubuntu, Linux aarch64).
JDK 25 is the current LTS and uses the same JVMTI surface.
