# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Coz is a causal profiler for native code (C/C++/Rust) that uses performance experiments to predict optimization impact. Unlike traditional profilers, it measures "bang for buck" - showing how optimizing a line affects overall throughput or latency.

### Why Causal Profiling?

Traditional profilers measure **where** programs spend time (observational), but not **whether** optimizing that code will improve overall performance (causal). Key limitations of traditional profiling:

- **Serial programs**: High CPU-consuming code may not be on the critical path
- **Parallel programs**: Optimizing one thread may just cause it to wait longer at synchronization points
- **Misleading hotspots**: Code that consumes CPU time may not drive end-to-end performance

Causal profiling answers: *"If I optimize this code, will my program actually run faster?"*

The approach uses **controlled performance experiments** with virtual speedups, establishing causality rather than just correlation between code performance and program throughput.

## Build System

The project uses CMake for building both the profiler library and benchmarks.

### Building Coz

```bash
# Build the profiler
cmake .
make
sudo make install
sudo ldconfig

# Build with benchmarks (requires debug info)
cmake -DBUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo .
make
```

**Important**: Benchmarks must be built with debug information (`Debug` or `RelWithDebInfo`). The build will fail if you try to build benchmarks without debug info.

> **DWARF reminder:** the vendored libelfin + `memory_map` logic now handle DWARF 2–5 line tables. DWARF 5 uses an explicit file table (no implicit “file 0 = CU source”), so `line_table::entry::file_index` starts at 0 in that mode. If you touch either libelfin or `libcoz/inspect.cpp`, keep their assumptions in sync and always test against a DWARF‑5 build (GCC’s default on modern Linux).

### Building Individual Benchmarks

Benchmarks are in `benchmarks/` and each has its own CMakeLists.txt:

```bash
cd benchmarks
cmake .
make
# Run specific benchmark with coz
coz run --- ./toy/toy
```

## Architecture

### Core Components

1. **libcoz** (`libcoz/`): The profiler library loaded as a MODULE (not linked directly)
   - `profiler.cpp/h`: Main profiler singleton, experiment orchestration
   - `thread_state.h`: Per-thread state for delay tracking
   - `progress_point.h`: Throughput and latency point tracking
   - **Platform-specific sampling**:
     - `perf.cpp/h`: Linux implementation using perf_event_open syscall
     - `perf_macos.cpp/h`: macOS implementation using kperf private framework
   - `inspect.cpp/h`: DWARF debug info parsing using libelfin (supports DWARF 2-5). Source filtering obeys `COZ_SOURCE_SCOPE` (defaults to `%`) and the optional `COZ_FILTER_SYSTEM=1` env to drop `/usr/include`, `/usr/lib`, etc. Keep those flags in mind before hard-coding additional filters.
   - `real.cpp/h`: Wrapper functions to capture real pthread/libc functions
   - `libcoz.cpp`: Library initialization and exported symbols

2. **coz script** (`coz`): Python 3 command-line wrapper
   - Handles `coz run` and `coz plot` commands
   - Sets up LD_PRELOAD to inject libcoz.so
   - Manages profiler environment variables

3. **include/coz.h**: Instrumentation macros for target programs
   - `COZ_PROGRESS` / `COZ_PROGRESS_NAMED`: Throughput progress points
   - `COZ_BEGIN(name)` / `COZ_END(name)`: Latency measurement points
   - Uses weak dlsym to locate `_coz_get_counter` at runtime

### Causal Profiling Mechanism

Coz uses **virtual speedups** to measure optimization potential causally rather than observationally. Instead of actually optimizing code, it simulates the effect by slowing down everything else proportionally.

#### Virtual Speedup Algorithm

The key insight: slowing down all other code has the same relative effect as speeding up the selected line. When a sample falls within a selected line of code, other threads pause proportionally.

**Delay-to-speedup relationship**: `Δt̄ = d/P`
- `d` = delay duration
- `P` = sampling period (1ms)
- Example: Inserting a delay that is 25% of the sampling period virtually speeds up the line by 25%

**Effective runtime**: `t̄ₑ = t̄ · (1 - d/P)`
- This accounts for the sampling-based approach where not every execution is instrumented

#### Experimental Methodology

For each experiment, Coz:
1. **Randomly selects** a virtual speedup from 0-100% in 5% increments (`SpeedupDivisions = 20`)
2. **Baseline weighting**: 0% speedup selected with 50% probability (`ZeroSpeedupWeight = 7`), remaining speedup values split the other 50%
3. **Measures impact** on progress point visit rates

**Measurement formula**: `1 - (ps/p0)`
- `p0` = period between progress point visits with no speedup (baseline)
- `ps` = period measured with virtual speedup s
- Result shows percent improvement in throughput

#### Sampling Implementation

Instead of instrumenting every line execution (prohibitively expensive):
- Samples program counter every 1ms (`SamplePeriod = 1000000`)
- Counts samples falling in the selected line
- Other threads delay proportionally to sample count
- Processes samples in batches of 10 (`SampleBatchSize = 10`) every 10ms for efficiency

**Number of samples**: `s ≈ n · t̄/P`
- `n` = execution count
- `t̄` = average runtime per execution
- Approximates how often the line would be sampled

#### Counter-Based Delay Coordination

Rather than expensive POSIX signals, Coz uses atomic counters:
- **Global counter**: Total pauses required across all threads (`_global_delay`)
- **Local counters**: Each thread's completed pause count (`thread_state->local_delay`)
- Threads pause when `local_delay < _global_delay`
- To signal pauses: increment both global counter and own local counter

#### Blocking Operations Handling

Coz intercepts blocking operations (`pthread_mutex_lock`, `pthread_cond_wait`, blocking I/O) to ensure correctness:

**Rule**: If thread A resumes thread B from blocking, thread B should be credited for delays inserted in thread A.

**Implementation** (see `profiler.h`):
- `pre_block()`: Records current global delay time
- `post_block(skip_delays)`: If thread was woken by another thread, skip delays inserted during blocked period
- `catch_up()`: Forces threads to execute all required delays before potentially unblocking other threads

This prevents virtual speedup measurements from being distorted by blocking behavior.

#### Progress Point Tracking

**Throughput profiling**: Measures rate of visits to progress points
- Use `COZ_PROGRESS` or `COZ_PROGRESS_NAMED("name")`
- Coz tracks visit frequency changes across experiments

**Latency profiling**: Measures time between paired progress points using Little's Law
- `W = L/λ` where:
  - `W` = average latency
  - `L` = average number of requests in progress
  - `λ` = arrival rate (throughput)
- Use `COZ_BEGIN("name")` and `COZ_END("name")`
- Allows latency measurement without tracking individual transactions

#### Phase Adjustment

For programs with distinct execution phases, Coz applies a correction factor:

`ΔP = Δpₐ · (tₒbₛ/sₒbₛ) · (s/T)`

This prevents overstating speedup potential for code that only executes during certain program phases.

Key constants in `profiler.h`:
- `SamplePeriod = 1000000` (1ms between samples)
- `SampleBatchSize = 10` (process every 10ms)
- `SpeedupDivisions = 20` (5% speedup increments)
- `ZeroSpeedupWeight = 7` (~25% of experiments at 0% baseline)
- `ExperimentMinTime = SamplePeriod * SampleBatchSize * 50` (minimum experiment duration)
- `ExperimentCoolOffTime = SamplePeriod * SampleBatchSize` (cooldown between experiments)
- `ExperimentTargetDelta = 5` (minimum progress point visits per experiment)

### Thread Management

- Profiler wraps `pthread_create` to inject delay tracking into new threads
- Each thread has a `thread_state` with local delay counters
- Global atomic delay counter (`_global_delay`) coordinates experiments
- Pre/post-block hooks skip delays during blocking operations

## Common Development Tasks

### Running Coz

Programs must be compiled with debug info (`-g`) and linked with `-ldl`. Coz understands modern DWARF line tables (up through DWARF 5), so you can rely on your toolchain's default DWARF version.

```bash
# Build target with debug info
g++ -g -o myapp myapp.cpp -ldl

# Run with coz
coz run --- ./myapp

# View results (opens browser or use hosted viewer)
coz plot
# Or visit: https://coz-profiler.github.io/coz-ui/
```

If you only want to collect lines from your own sources (and not the C++ standard library), pass one or more `--source-scope` globs or set `COZ_SOURCE_SCOPE`. Coz also honors `COZ_FILTER_SYSTEM=1` as a quick toggle to drop system headers after the DWARF pass. For example:

```bash
# Limit to project files
coz run --source-scope '/media/psf/Home/git/coz-portage/benchmarks/**' --- ./benchmarks/toy/toy

# Or just drop system headers entirely
COZ_FILTER_SYSTEM=1 coz run --- ./benchmarks/toy/toy
```

### Adding Progress Points

Include `include/coz.h` and add macros:

```c++
#include "coz.h"

// Throughput: mark end of work unit
void process_request() {
    // ... do work ...
    COZ_PROGRESS;  // Or COZ_PROGRESS_NAMED("requests")
}

// Latency: mark transaction boundaries
void handle_transaction() {
    COZ_BEGIN("transaction");
    // ... do work ...
    COZ_END("transaction");
}
```

### Rust Support

Coz has Rust bindings in `rust/`:
- Cargo crate published as `coz`
- Use `coz::progress!()`, `coz::scope!()`, `coz::begin!()`, `coz::end!()`
- Must compile with debug info: `debug = 1` in Cargo.toml `[profile.release]`
- Run with: `coz run --- ./target/release/binary`

### CMake Integration

Projects can import coz targets:

```cmake
find_package(coz-profiler)
# Provides coz::coz (library+includes) and coz::profiler (binary)
target_link_libraries(myapp PRIVATE coz::coz)
```

## System Requirements

### Linux
- Linux 2.6.32+ with `perf_event_open` support
- Set perf paranoia: `echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid`
- Dependencies: libdwarf-dev, libelfin, pthread, dl, rt

### macOS
- macOS 10.10+ (kperf framework availability)
- Requires elevated privileges or SIP adjustments for kperf access
- Dependencies: libelfin, pthread, dl
- **Important**: Uses private kperf API which may change without notice
- Cannot be used in App Store applications due to private API usage

## Dependencies

- **libelfin**: DWARF/ELF parsing (must be installed from https://github.com/plasma-umass/libelfin)
- Build requires: CMake, C++11 compiler, Python 3, pkg-config
- Benchmark dependencies: libbz2-dev, libsqlite3-dev

## Output Format

Profiler writes to `profile.coz` (configurable). Format includes:
- Experiment records: speedup%, source location, progress delta, duration
- Loaded at https://coz-profiler.github.io/coz-ui/ for visualization

### Interpreting Results

Coz profiles show the predicted program speedup (y-axis) from optimizing each line by the percentage on the x-axis. Lines are sorted by linear regression slope:

**Steep upward slopes** (positive correlation):
- Strong optimization candidates
- Optimizing this code directly improves overall performance
- Example: A line with slope +0.8 means optimizing it by 10% improves program throughput by ~8%

**Flat or near-zero slopes**:
- Optimization won't improve overall performance
- Code may be fast already or not on critical path
- Investment in optimizing these lines has minimal return

**Downward slopes** (negative correlation):
- Counter-intuitive result indicating contention
- Optimizing might harm performance (e.g., lock contention increases)
- May indicate the real bottleneck is elsewhere

**100% virtual speedup**: Represents completely removing the line's runtime (theoretical upper bound).

### Performance Overhead

From the SOSP 2015 paper evaluation:
- Mean overhead: **17.6%** across benchmarks
- Delay insertion contributes: **10.2 percentage points**
- Sampling and bookkeeping: **~7 percentage points**
- Overhead is primarily from the virtual speedup mechanism itself, not instrumentation

## Known Results from the Paper

The authors demonstrated significant speedups on real applications:
- **Memcached**: 9% improvement
- **SQLite**: 25% improvement
- **PARSEC benchmarks**: Up to 68% acceleration

**Example optimization** (SQLite): Identified three functions with high indirect call overhead. Converting function pointers to direct calls yielded measurable gains despite minimal computational work in the functions themselves.

## Limitations

### Platform-Specific

**Linux**:
- Requires perf_event_open support (kernel 2.6.32+)
- Needs appropriate perf_event_paranoid settings

**macOS**:
- Uses private kperf API (may break in future OS versions)
- Requires elevated privileges or SIP configuration
- Cannot be distributed via Mac App Store
- Sampling implementation differs from Linux (less detailed)

### General
- Requires DWARF debug information (supports DWARF 3, 4, and 5)
- No support for interpreted languages (Python, Ruby, JavaScript)
- JIT languages need debug info support (not currently implemented)
- Programs must have meaningful progress points for accurate profiling

## References

**Original Paper**: Charlie Curtsinger and Emery D. Berger. 2015. "Coz: Finding Code that Counts with Causal Profiling." In Proceedings of the 25th Symposium on Operating Systems Principles (SOSP '15). ACM. DOI: 10.1145/2815400.2815409

- Paper: https://arxiv.org/abs/1608.03676
- Received Best Paper Award at SOSP 2015
- Project homepage: https://github.com/plasma-umass/coz
- Web viewer: https://coz-profiler.github.io/coz-ui/
