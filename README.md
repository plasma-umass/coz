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

More details are available in our [Tech Report (pdf)](https://web.cs.umass.edu/publication/docs/2015/UM-CS-2015-008.pdf). A version of this paper will appear at SOSP 2015 in October.

## Requirements
Coz, our prototype causal profiler, runs with unmodified Linux executables. Coz requires:

- [Python](http://www.python.org)
- [Clang 3.1 or newer](http://clang.llvm.org) or another compiler with C++11 support
- [Linux](http://kernel.org) version 2.6.32 or newer (must support the `perf_event_open` system call)

## Building
To build Coz, just clone this repository and run `make`. The build system will check out other build dependencies and install them locally in the `deps` directory.

## Using Coz
Before running your program with Coz, you will need to identify one or more progress points. These are points in your program that you would like to happen more frequently. For example, in the `pbzip2` program under `benchmarks/pbzip2` we have inserted a progress point after the code that compresses a block of data.

To add a progress point, add the `COZ_PROGRESS` macro to the line you would like to execute more frequently. This macro is defined in `include/coz.h`. You can also mark transaction boundaries using the `COZ_BEGIN` and `COZ_END` macros. Coz will measure the average latency between these points, which allows you to evaluate potential optimizations by their impact on latency rather than throughput.

The `benchmarks` directory in this repository includes several small benchmarks with progress points added at appropriate locations. To build and run one of these benchmarks with `coz`, just browse to `benchmarks/{bench name}` and type `make bench` (or `make test` for a smaller input size). Note that most of these applications will download large input datasets before they can be run. These programs may require several runs before coz has enough measurements to generate a useful profile. Benchmarks include several [PHOENIX](https://github.com/kozyraki/phoenix) applications and [pbzip2](http://compression.ca/pbzip2/), which are licensed separately and included with this release for convenience.

To run an arbitrary program with Coz, just type `coz run --- <your program and arguments>` on the command line. You can specify profiling options befor the `---`. Run `coz run -h` for a description of the available options. By default, profiling output is appended to a file named `profile.coz` in the current directory.

## Processing Results
To plot profile results, go to [plasma-umass.github.io/coz/](http://plasma-umass.github.io/coz/) and load your profile. This page also includes several sample profiles from PARSEC benchmarks.

## License
All source code is licensed under the GPLv2 unless otherwise indicated. Copyright (C) 2015 University of Massachusetts Amherst
