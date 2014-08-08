# Coz: Causal Profiling
Causal profiling is a novel technique to measure optimization potential.
This measurement matches developers' assumptions about profilers: that
optimizing highly-ranked code will have the greatest impact on 
performance. Causal profiling measures optimization potential for serial, 
parallel, and asynchronous programs without instrumentation of special 
handling for library calls and concurrency primitives. Instead, a causal
profiler uses performance experiments to predict the effect of
optimizations. This allows the profiler to establish causality: 
"optimizing function X will have effect Y," exactly the measurement 
developers had assumed they were getting all along.

## Requirements
Coz, our prototype causal profiler, runs with unmodified Linux executables. Building and running Coz requires:

- [Clang 3.1 or newer](http://clang.llvm.org) or another compiler with C++11 support
- [libudis86](http://udis86.sourceforge.net) version 1.7.2 or newer
- [Linux](http://kernel.org) version 2.6.32 or newer, including the `perf_event` API

## Building
To build Coz, just clone this repository and run `make release`. This is just a prototype, so installing profiling support system-wide is not recommended.

## Using Coz
This repository includes sample applications in the `tests` directory, which you can run with Coz by typing `make tests` at the project root. To profile arbitrary applications, just preload the causal profiler library:

    LD_PRELOAD=/path/to/causal/libcausal.so <your command here>

A driver script will be available soon.

## Interpreting Results
TODO: Results processing scripts

## How it Works

- On startup, read configuration from environment variables
  - CAUSAL_INCLUDE: colon-separated list of file names (or name substrings). Only the main executable will be profiled by default.
  - Coming later: CAUSAL_PROGRESS_POINTS: colon-separated list of extra progress points
    - file-throughput
    - file-latency
- Read /proc/self/maps to locate all executable memory
- Open each executable ELF file and locate each function
- Disassemble each function to locate all basic blocks
  - Each basic block goes into a map from address range -> block stats
- Start sampling every N cycles in each thread. On each cycle:
  - If in idle mode (the initial state):
    - Locate the basic block where the sample occurred
    - If the block is in one of the included binaries, set this as the selected block and enter "speedup" mode
  - In speedup mode:
    - On each visit to the selected block, a global trip count is incremented.
    - Each thread keeps a local pause count. On every cycle sample, the thread must pause for (<global trip count> - <local pause count>) * <delay size>
    - Trip counts are collected using watchpoint sampling