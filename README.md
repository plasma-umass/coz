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
Coz, our prototype causal profiler, runs with unmodified Linux executables. Coz requires:

- [SCons](http://scons.org), a python-based build system
- [Clang 3.1 or newer](http://clang.llvm.org) or another compiler with C++11 support
- [Linux](http://kernel.org) version 2.6.32 or newer, including the `perf_event` API
- [Boost](http://boost.org) filesystem, program_options, and system libraries

## Building
To build Coz, just clone this repository and run `scons`. Adding `mode=release` will build an optimized version with less debug logging.

You can install Coz by running `scons install`, with an optional `prefix=<install prefix>` argument if you do not want to install to `/usr/local`.

## Using Coz
Before running your program with Coz, you will need to identify one or more progress points. These are points in your program that you would like to happen more frequently. For example, in the `pbzip2` program under `benchmarks/pbzip2` we have inserted a progress point after the code that compresses a block of data.

To add a progress point, add the `CAUSAL_PROGRESS` macro to the line you would like to execute more frequently. This macro is defined in `causal.h`, which is installed to `<prefix>/include` (`/usr/local/include` by default).

To run a program with Coz, just type `coz --- <your program and arguments>` on the command line. You can specify profiling options befor the `---`. Run `coz --help` for a description of the available options. Profiling output is placed in the file `profile.log` by default.

## Processing Results
To process results, run `coz-process profile.log`. This will generate a `profile.csv` file with predicted program speedups for all lines in the causal profile. To graph these results, run `coz-plot profile.csv`.

The `coz-plot` tool requires [R](http://r-project.org), along with the `ggplot2` and `plyr` packages. To install these, run R and type the command `install.packages('ggplot2', 'plyr')`.

Note that `coz-plot` may produce error messages if there are not enough samples in the profile to produce a plot. You can execute the profiled application several times to collect additional samples.
