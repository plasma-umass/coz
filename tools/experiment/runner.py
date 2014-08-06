#!/usr/bin/env python

import os
import sys
import time

BENCHMARKS = {
  'vips': 'native',
  'blackscholes': 'native',
  'canneal': 'native',
  'ferret': 'native',
  'facesim': 'simlarge',
  'streamcluster': 'test',
  'raytrace': 'simlarge',
  'swaptions': 'native',
  'bodytrack': 'native',
  'fluidanimate': 'native',
  'dedup': 'native',
  'freqmine': 'simlarge',
  'x264': 'native'
}

COZ = '/home/charlie/Projects/causal/release/bin/causal'
PARSEC_DIR = '/home/charlie/Projects/benchmarks/parsec-2.1'
PARSECMGMT = PARSEC_DIR + '/bin/parsecmgmt'

def build(benchmark, config, rebuild=False):
  # Clean and build the benchmark
  if rebuild:
    os.system(PARSECMGMT + ' -a uninstall -c ' + config + ' -p ' + benchmark + ' > /dev/null')
  os.system(PARSECMGMT + ' -a build -c ' + config + ' -p ' + benchmark + ' > /dev/null')

def run(benchmark, config, output_file=None, coz=True, runs=3, threads=64, size='native', fixed_line=None, fixed_speedup=None, end_to_end=False, keep_inputs=False, show_output=False, sample_only=False):
  
  if coz:
    runner = [COZ, '--include ' + PARSEC_DIR, '--output ' + output_file]
    if fixed_line != None:
      runner.append('--fixed-line ' + fixed_line)
  
    if fixed_speedup != None:
      runner.append('--fixed-speedup ' + str(fixed_speedup))
  
    if end_to_end:
      runner.append('--end-to-end')
    
    if sample_only:
      runner.append('--sample-only')
      
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
    print >> sys.stderr, elapsed
    runtimes.append(elapsed)
  
  return runtimes
