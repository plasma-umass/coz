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

### Profiling Modes

Coz departs from conventional profiling by making it possible to view the effect of optimizations on both throughput and latency. To profile throughput, you must specify a progress point. To profile latency, you must specify a pair of progress points. 

#### Throughput Profiling: Specifying Progress Points

To profile throughput you must indicate a line in the code that corresponds to the end of a unit of work. For example, a progress point could be the point at which a transaction concludes, when a web page finishes rendering, or when a query completes. Coz then measures the rate of visits to each progress point to determine any potential optimization's effect on throughput. 

To place a progress point, include `coz.h` (under the `include` directory in this repository) and add the `COZ_PROGRESS` macro to at least one line you would like to execute more frequently.

By default, Coz uses the source file and line number as the name for your progress points. If you use `COZ_PROGRESS_NAMED("name for progress point")` instead, you can provide an informative name for your progress points. This also allows you to mark multiple source locations that correspond to the same progress point.

#### Latency Profiling: Specifying Progress Points

To profile latency, you must place two progress points that correspond to the start and end of an event of interest, such as when a transaction begins and completes. Simply  mark the beginning of a transaction with the `COZ_BEGIN("transaction name")` macro, and the end with the `COZ_END("transaction name")` macro. Unlike regular progress points, you always need to specify a name for your latency progress points.

When coz tests a hypothetical optimization it will report the effect of that optimization on the average latency between these two points. Coz can track this information with any knowledge of individual transactions thanks to [Little's Law](https://en.wikipedia.org/wiki/Little%27s_law).

### Specifying Progress Points on the Command Line
Coz has command line options to specify progress points when profiling the application instead of modifying its source. This feature is currently disabled because it did not work particularly well. Adding support for better command line-specified progress points is planned in the near future.

## Processing Results
To plot profile results, go to http://plasma-umass.github.io/coz/ and load your profile. This page also includes several sample profiles from PARSEC benchmarks.

## Sample Applications
The `benchmarks` directory in this repository includes several small benchmarks with progress points added at appropriate locations. To build and run one of these benchmarks with `coz`, just browse to `benchmarks/{bench name}` and type `make bench` (or `make test` for a smaller input size). These programs may require several runs before coz has enough measurements to generate a useful profile. Once you have profiled these programs for several minutes, go to http://plasma-umass.github.io/coz/ to load and plot your profile.

## License
All source code is licensed under the BSD 2-clause license unless otherwise indicated.

Copyright (C) 2015 Charlie Curtsinger and Emery Berger, University of Massachusetts Amherst

Sample applications (in the `benchmarks` directory) include several [PHOENIX](https://github.com/kozyraki/phoenix) programs and [pbzip2](http://compression.ca/pbzip2/), which are licensed separately and included with this release for convenience.
