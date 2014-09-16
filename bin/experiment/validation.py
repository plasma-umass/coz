#!/usr/bin/env python

import os
import runner

OUTPUT_DIR = '/home/charlie/Projects/causal/tools/experiment/validation_results'

for (benchmark, input_size) in runner.BENCHMARKS.items():
  runner.build(benchmark, 'gcc')
  
  output_file = OUTPUT_DIR + '/' + benchmark + '.log'
  
  # Remove the old output file
  if os.path.isfile(output_file):
    os.remove(output_file)
  
  # Generate line counts
  print 'Running', benchmark
  runtime = runner.run(benchmark=benchmark, config='gcc', output_file=output_file, runs=3, fixed_speedup=0, end_to_end=True, size=input_size)
  os.system('coz-process ' + output_file)
