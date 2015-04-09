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

More details are available in our [Tech Report (pdf)](https://web.cs.umass.edu/publication/docs/2014/UM-CS-2014-018.pdf).

## Requirements
Coz, our prototype causal profiler, runs with unmodified Linux executables. Coz requires:

- [Python](http://www.python.org), along with the [ggplot](https://pypi.python.org/pypi/ggplot) package (only required for plotting. See details below)
- [Clang 3.1 or newer](http://clang.llvm.org) or another compiler with C++11 support
- [Linux](http://kernel.org) version 2.6.32 or newer (must support the `perf_event_open` system call)

On debian or ubuntu, you can install all build dependencies with the following lines:

```
sudo apt-get install clang linux-tools
```

## Building
To build Coz, just clone this repository and run `make`. The build system will check out other build dependencies and install them locally in the `deps` directory.

## Using Coz
Before running your program with Coz, you will need to identify one or more progress points. These are points in your program that you would like to happen more frequently. For example, in the `pbzip2` program under `benchmarks/pbzip2` we have inserted a progress point after the code that compresses a block of data.

To add a progress point, add the `CAUSAL_PROGRESS` macro to the line you would like to execute more frequently. This macro is defined in `include/causal.h`. You can also mark transaction boundaries using the `CAUSAL_BEGIN` and `CAUSAL_END` macros. Coz will measure the average latency between these points, which allows you to evaluate potential optimizations by their impact on latency rather than throughput.

To run a program with Coz, just type `coz run --- <your program and arguments>` on the command line. You can specify profiling options befor the `---`. Run `coz run -h` for a description of the available options. Profiling output is placed in the file `profile.coz` by default.

## Processing Results
To plot the results from a causal profile, run `coz plot`. This will generate an image from the `profile.coz` file in the current directory. This functionality requires the Python `ggplot` package. You can install this package with the command `pip install ggplot`.

If you do not have `ggplot` installed, you can generate a CSV from the causal profile results using the `coz process` command. The output file `profile.csv` can be used with most spreadsheet programs.

## License
All source code is licensed under the GPLv2 unless otherwise indicated. Copyright (C) 2014 University of Massachusetts Amherst
