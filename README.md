# Coz: Finding Code that Counts with Causal Profiling

Coz is a new kind of profiler that unlocks optimization opportunities missed by traditional profilers. Coz employs a novel technique we call *causal profiling* that measures optimization potential.
This measurement matches developers' assumptions about profilers: that
optimizing highly-ranked code will have the greatest impact on
performance. Causal profiling measures optimization potential for serial,
parallel, and asynchronous programs without instrumentation of special
handling for library calls and concurrency primitives. Instead, a causal
profiler uses performance experiments to predict the effect of
optimizations. This allows the profiler to establish causality:
"optimizing function X will have effect Y," exactly the measurement
developers had assumed they were getting all along.

Full details of Coz are available in our paper, [Coz: Finding Code
that Counts with Causal Profiling
(pdf)](Coz-Curtsinger-Berger-SOSP2015.pdf), SOSP 2015, October 2015
(recipient of a Best Paper Award).

## Requirements
Coz, our prototype causal profiler, runs with unmodified Linux executables. Coz requires:

- [Python](http://www.python.org)
- [Clang 3.1 or newer](http://clang.llvm.org) or another compiler with C++11 support
- [Linux](http://kernel.org) version 2.6.32 or newer (must support the `perf_event_open` system call)

## Building
To build Coz, just clone this repository and run `make`. The build system will check out other build dependencies and install them locally in the `deps` directory.

## Using Coz
Using coz requires a small amount of setup, but you can jump ahead to the section on the included [sample applications](#sample-applications) in this repository if you want to try coz right away.

To run your program with coz, you will need to build it with debug information. You do not need to include debug symbols in the main executable: coz uses the same procedure as `gdb` to locate debug information for stripped binaries.

Once you have your program built with debug information, you can run it with coz using the command `coz run {coz options} --- {program name and arguments}`. But, to produce a useful profile you need to decide which part(s) of the application you want to speed up by specifying one or more progress points.

### Progress Points
A progress point is a line of code in your program that you would like to execute more frequently. For example, we have included a version of pbzip2 with a progress point inserted just after the code that compresses a block of data. Coz will evaluate any hypothetical optimizations based on how they impact the rate of visits to this line of code. There are three ways to specify progress points in your program: end-to-end, source-level, or sampling based.

#### End-to-End Progress Points
If you run your program with `coz run --end-to-end --- {program name and arguments}`, coz does not require you to specify any progress points. Instead, it will take the full execution of the program to run a single experiment, which tests the effect of speeding up one line by a specific amount on the program's total runtime. Because coz is limited to a single experiment per-run, this method will take a very long time to build a useful profile.

#### Source-Level Progress Points
This is the preferred method of specifying progress points in your application. Just include `coz.h` (under the `include` directory in this repository) and add the `COZ_PROGRESS` macro to at least one line you would like to execute more frequently.

Source-level progress points also allow you to measure the effect of optimizations on latency. Just mark the beginning of a transaction with the `COZ_BEGIN` macro, and the end with the `COZ_END` macro. When coz tests a hypothetical optimization it will report the effect of that optimization on the average latency between these two points. Coz can track this information with any knowledge of individual tranactions thanks to [Little's Law](https://en.wikipedia.org/wiki/Little%27s_law).

#### Sampling-Based Progress Points
You can also specify progress points on the command line using the `--progress {source file}:{line number}` argument to `coz run`. This method uses sampling to estimate the rate of visits to the specified progress point. Choosing an appropriate line can be difficult because of inconsistencies in hardware performance counter results and debug information (especially for optimized executables). The best way to select a line is to run your program with `coz run` using the `--end-to-end` argument, then use the profile results to identify a frequency executed line

### Processing Results
To plot profile results, go to http://plasma-umass.github.io/coz/ and load your profile. This page also includes several sample profiles from PARSEC benchmarks.

## Sample Applications
The `benchmarks` directory in this repository includes several small benchmarks with progress points added at appropriate locations. To build and run one of these benchmarks with `coz`, just browse to `benchmarks/{bench name}` and type `make bench` (or `make test` for a smaller input size). These programs may require several runs before coz has enough measurements to generate a useful profile. Once you have profiled these programs for several minutes, go to http://plasma-umass.github.io/coz/ to load and plot your profile.

## License
All source code is licensed under the BSD 2-clause license unless otherwise indicated. Copyright (C) 2015 University of Massachusetts Amherst

Sample applications include several [PHOENIX](https://github.com/kozyraki/phoenix) programs and [pbzip2](http://compression.ca/pbzip2/), which are licensed separately and included with this release for convenience.
