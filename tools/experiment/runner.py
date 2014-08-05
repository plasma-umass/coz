#!/usr/bin/env python

import os
import sys
import time

BENCHMARKS = {
  'vips': 'native',               # 11 seconds
  'blackscholes': 'native',       # 36 seconds
  'canneal': 'simlarge',          # 7 seconds
  'ferret': 'native',             # 39 seconds
  'facesim': 'simmedium',         # 8 seconds
  'streamcluster': 'test',        # 9 seconds
  'raytrace': 'simlarge',         # 9 seconds
  'swaptions': 'native',          # 13 seconds
  'bodytrack': 'simlarge',        # 2 seconds
  'fluidanimate': 'simmedium',    # 26 seconds
  'dedup': 'simlarge',            # 20 seconds
  'freqmine': 'simlarge'          # 11 seconds
  
  #'x264': 'native'               # 14 seconds < no debug symbols. Come back to this later
}

COZ = '/home/charlie/Projects/causal/release/bin/causal'
PARSEC_DIR = '/home/charlie/Projects/benchmarks/parsec-2.1'
PARSECMGMT = PARSEC_DIR + '/bin/parsecmgmt'

def build(benchmark, config, rebuild=False):
  # Clean and build the benchmark
  if rebuild:
    os.system(PARSECMGMT + ' -a uninstall -c ' + config + ' -p ' + benchmark + ' > /dev/null')
  os.system(PARSECMGMT + ' -a build -c ' + config + ' -p ' + benchmark + ' > /dev/null')

def run(benchmark, config, output_file=None, coz=True, runs=3, threads=64, size='native', fixed_line=None, fixed_speedup=None, end_to_end=False, keep_inputs=False, show_output=False):
  
  if coz:
    runner = [COZ, '--include ' + PARSEC_DIR, '--output ' + output_file]
    if fixed_line != None:
      runner.append('--fixed-line ' + fixed_line)
  
    if fixed_speedup != None:
      runner.append('--fixed-speedup ' + str(fixed_speedup))
  
    if end_to_end:
      runner.append('--end-to-end')
      
    runner.append(' --- ')
    
  else:
    runner = ['time']
  
  command_parts = [
    PARSECMGMT,
    '-a run',
    '-c ' + config,
    '-p ' + benchmark,
    '-i ' + size,
    '-n ' + str(threads),
    '-s \"' + ' '.join(runner) + '\"'
  ]
  
  if keep_inputs:
    command_parts.append('-k')

  # Run the benchmark with causal, no speedups
  command = ' '.join(command_parts)
  
  if not show_output:
    command += ' > /dev/null'
  
  runtimes = []
  
  for i in range(0, runs):
    start_time = time.time()
    os.system(command)
    elapsed = time.time() - start_time
    runtimes.append(elapsed)
  
  return runtimes
