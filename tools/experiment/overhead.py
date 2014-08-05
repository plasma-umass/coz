#!/usr/bin/env python

import os
import sys
import runner

sys.path.append('/home/charlie/Projects/causal/tools/coz-process')
import coz_profile

RUNS = 3
OUTPUT_DIR = '/home/charlie/Projects/causal/tools/experiment/overhead_results'

def to_seconds(ns):
  return float(ns/1000) / 1000000

exists = os.path.isfile('overhead.csv')

results = open('overhead.csv', 'a')

if not exists:
  print >> results, 'benchmark,configuration,runtime'

for (benchmark, input_size) in runner.BENCHMARKS.items():
  print >> sys.stderr, 'Benchmark:', benchmark
  
  runner.build(benchmark, 'gcc')
  
  output_file = OUTPUT_DIR + '/' + benchmark + '.log'
  
  # Run once to ensure inputs are unpacked
  # Not required, now that everything is ready
  #runner.run(benchmark=benchmark, config='gcc', coz=False, runs=1, size=input_size)
  
  print >> sys.stderr, 'Starting clean runs'
  times = runner.run(benchmark=benchmark,
                     config='gcc',
                     size=input_size,
                     runs=RUNS,
                     coz=False,
                     keep_inputs=True)
  for t in times:
    print >> results, ','.join([benchmark, 'clean', str(t)])
  results.flush()
  
  print >> sys.stderr, 'Starting zero delay runs'
  times = runner.run(benchmark=benchmark,
                     config='gcc',
                     runs=RUNS,
                     size=input_size,
                     output_file=output_file,
                     fixed_speedup=0,
                     keep_inputs=True)
  
  for t in times:
    print >> results, ','.join([benchmark, 'zero-delay', str(t)])
  results.flush()

  # Remove the output file
  if os.path.isfile(output_file):
    os.remove(output_file)

  print >> sys.stderr, 'Starting full profile runs'
  times = runner.run(benchmark=benchmark,
                     config='gcc',
                     runs=RUNS,
                     size=input_size,
                     output_file=output_file,
                     keep_inputs=True)

  for t in times:
    print >> results, ','.join([benchmark, 'full', str(t)])
  
  p = profile()
  p.process_file(output_file)
  for t in p.run_times:
    print >> results, ','.join([benchmark, 'main', str(to_seconds(t))])
  
  results.flush()
