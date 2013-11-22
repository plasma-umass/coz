# Causal Profiling
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
Our prototype causal profiler runs on Linux and Mac OSX. The profiler
relies on the LLVM infrastructure to insert profiling instrumentation.
Development is done using LLVM 3.3, but other recent releases should be
compatible.

## Building
To build the profiler, you first need the source for LLVM 3.3.

### Getting LLVM source code
Check out the LLVM source code:
```svn co http://llvm.org/svn/llvm-project/llvm/trunk llvm```

*Recommended* - Check out clang:
```bash
cd llvm/tools
svn co http://llvm.org/svn/llvm-project/cfe/trunk clang
cd ../projects
svn co http://llvm/org/svn/llvm-project/compiler-rt/trunk compiler-rt
cd ../..
```

### Getting Causal
The code for this project should be cloned into the `llvm/projects`
directory. Starting from your original location:
```bash
cd llvm/projects
git clone http://github.com/plasma-umass/causal causal
cd ../..
```

### Configure and build
LLVM has many different configuration options. The options below are 
reasonable defaults:
```bash
cd llvm
./configure --prefix=/usr/local --enable-optimized --enable-assertions --enable-shared
make install
```

### OSX Workarounds
The causal profiler uses C++11 support, including some functionality
only available in the LLVM project's libc++ runtime library on OSX. To obtain
and use libc++, follow the directions available at http://libcxx.llvm.org.

## Using the Profiler
The Makefile setup in `tests/common.mk` builds all test applications for
causal profiling. To see the exact commands executed, move to an application
directory under `tests` and run `make -n`.

Profiler output includes results from both slowdown and speedup experiments.
Slowdown results include symbol name, file name, and line number information
for each block, if available. Speedup results are in CSV format, with columns
for block name, block speedup, and performance change. These results can be
loaded using your favorite spreadsheet or graphing program.
