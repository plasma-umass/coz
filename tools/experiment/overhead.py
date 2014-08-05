#!/usr/bin/env python

import os
import sys
import runner

RUNS = 5
OUTPUT_DIR = '/home/charlie/Projects/causal/tools/experiment/overhead_results'

clean_runtimes = {}
zero_delay_runtimes = {}
total_runtimes = {}

for (benchmark, input_size) in runner.BENCHMARKS.items():
  print >> sys.stderr, 'Benchmark:', benchmark
  
  runner.build(benchmark, 'gcc')
  
  output_file = OUTPUT_DIR + '/' + benchmark + '.log'
  
  # Run once to ensure inputs are unpacked
  runner.run(benchmark=benchmark, config='gcc', coz=False, runs=1, size=input_size)
  
  print >> sys.stderr, 'Starting clean runs'
  clean_runtimes[benchmark] = runner.run(benchmark=benchmark,
                                         config='gcc',
                                         size=input_size,
                                         runs=RUNS,
                                         coz=False,
                                         keep_inputs=True)
  
  print >> sys.stderr, 'Starting zero delay runs'
  zero_delay_runtimes[benchmark] = runner.run(benchmark=benchmark,
                                              config='gcc',
                                              runs=RUNS,
                                              size=input_size,
                                              output_file=output_file,
                                              fixed_speedup=0,
                                              keep_inputs=True)

  # Remove the output file
  if os.path.isfile(output_file):
    os.remove(output_file)

  print >> sys.stderr, 'Starting full profile runs'
  total_runtimes[benchmark] = runner.run(benchmark=benchmark,
                                         config='gcc',
                                         runs=RUNS,
                                         size=input_size,
                                         output_file=output_file,
                                         keep_inputs=True)

def mean(xs):
  return float(sum(xs)) / len(xs)

print 'benchmark\tclean_runtime\tzero_delay_runtime\tfull_profile_runtime'

for benchmark in BENCHMARKS:
  clean = mean(clean_runtimes[benchmark])
  zero_delay = mean(zero_delay_runtimes[benchmark])
  total = mean(total_runtimes[benchmark])
  
  print benchmark + '\t' + str(clean) + '\t' + str(zero_delay) + '\t' + str(total)
