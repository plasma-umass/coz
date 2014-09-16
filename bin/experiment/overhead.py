#!/usr/bin/env python

import os
import sys
import runner

sys.path.append('/home/charlie/Projects/causal/tools/coz-process')
import coz_profile

RUNS = 4
OUTPUT_DIR = '/home/charlie/Projects/causal/tools/experiment/overhead_results'

def to_seconds(ns):
  return float(ns/1000) / 1000000

exists = os.path.isfile('overhead.csv')

results = open('overhead.csv', 'a')

if not exists:
  print >> results, 'benchmark,configuration,runtime'
  
benchmarks = runner.BENCHMARKS

if len(sys.argv) > 1:
  benchmarks = {}
  for bmk in sys.argv[1:]:
    benchmarks[bmk] = runner.BENCHMARKS[bmk]

for (benchmark, input_size) in benchmarks.items():
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
  
  # Remove the output file
  if os.path.isfile(output_file):
    os.remove(output_file)
  
  print >> sys.stderr, 'Starting zero delay runs'
  times = runner.run(benchmark=benchmark,
                     config='gcc',
                     runs=RUNS,
                     size=input_size,
                     output_file=output_file,
                     sample_only=True,
                     keep_inputs=True)
  
  # Open the profile
  p = coz_profile.profile()
  p.process_file(output_file)
  
  for (full_time, main_time) in zip(times, p.run_times):
    main_time = to_seconds(main_time)
    print >> results, ','.join([benchmark, 'startup_time', str(full_time - main_time)])
    print >> results, ','.join([benchmark, 'sampling_time', str(main_time)])
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

  # Open the profile
  p = coz_profile.profile()
  p.process_file(output_file)
  
  for (full_time, main_time) in zip(times, p.run_times):
    main_time = to_seconds(main_time)
    print >> results, ','.join([benchmark, 'startup_time', str(full_time - main_time)])
    print >> results, ','.join([benchmark, 'delay_time', str(main_time)])
  results.flush()
