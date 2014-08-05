#!/usr/bin/env python

import os
import sys
import time

COZ = '/home/charlie/Projects/causal/release/bin/causal'
PARSEC_DIR = '/home/charlie/Projects/benchmarks/parsec-2.1'
PARSECMGMT = PARSEC_DIR + '/bin/parsecmgmt'
OUTPUT_DIR = '/home/charlie/Projects/causal/tools/experiment/results'

BENCHMARKS = {
  'blackscholes': 'native',       # 35.87 seconds
  'canneal': 'simlarge',          # 7.04 seconds
  'dedup': 'simlarge',            # 21.91 seconds
  'ferret': 'native',             # 66.58 seconds
  'fluidanimate': 'simmedium',    # 38.41 seconds
  'freqmine': 'simlarge',         # 10.98 seconds
  'raytrace': 'simlarge',         # 8.98 seconds
  'swaptions': 'native',          # 13.16 seconds
  'vips': 'native',               # 24.00 seconds
  'x264': 'native',               # 23.59 seconds
  'facesim': 'simmedium',         # 10.13 seconds
  'streamcluster': 'test',        # 14.67 seconds
  'bodytrack': 'simlarge'         # 3.06 seconds
}

def main():
  for (benchmark, input_size) in BENCHMARKS.items():
    build(benchmark, 'gcc')
    output = run(benchmark, 'gcc', runs=3, fixed_speedup=0, end_to_end=True, size=input_size)
    os.system('coz-process ' + output)

def build(benchmark, config, rebuild=False):
  # Clean and build the benchmark
  if rebuild:
    print 'Cleaning', benchmark
    os.system(PARSECMGMT + ' -a uninstall -c ' + config + ' -p ' + benchmark + ' > /dev/null')
  print 'Building', benchmark
  os.system(PARSECMGMT + ' -a build -c ' + config + ' -p ' + benchmark + ' > /dev/null')

def run(benchmark, config, runs=3, threads=64, size='native', fixed_line=None, fixed_speedup=None, end_to_end=False, output_file=None):
  
  if output_file == None:
    output_file = OUTPUT_DIR + '/' + benchmark + '.log'
  
  if os.path.isfile(output_file):
    os.remove(output_file)
  
  runner = [COZ, '--include ' + PARSEC_DIR, '--output ' + output_file]
  if fixed_line != None:
    runner.append('--fixed-line ' + fixed_line)
  
  if fixed_speedup != None:
    runner.append('--fixed-speedup ' + str(fixed_speedup))
  
  if end_to_end:
    runner.append('--end-to-end')

  # Run the benchmark with causal, no speedups
  command = ' '.join([
    PARSECMGMT,
    '-a run',
    '-c ' + config,
    '-p ' + benchmark,
    '-i ' + size,
    '-n ' + str(threads),
    '-s \"' + ' '.join(runner) + ' --- \"'
  ])
  
  for i in range(0, runs):
    print 'Running', benchmark
    start_time = time.time()
    os.system(command + ' > /dev/null')
    print 'Took', (time.time() - start_time), 'seconds'
  
  return output_file

main()
