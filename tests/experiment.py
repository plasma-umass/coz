#!/usr/bin/env python

import time
import subprocess
import sys

runs = 5
program = sys.argv[1]
args = " ".join(sys.argv[2:])

def run_command(command, timelimit=0):
  p = subprocess.Popen(command, shell=True)
  if timelimit == 0:
    p.wait()
  else:
    elapsed = 0
    while p.poll() == None and elapsed < timelimit:
      time.sleep(1)
      elapsed += 1
    if p.poll() == None:
      p.kill()
      print 'Killed'
  
print 'Cleaning up old profile data'
run_command('rm -f block.profile versions.profile')

# Collect baseline run data
for run in range(0, runs):
  print 'Doing baseline run', run
  run_command(program + ' block_profile ' + args, 120)

# Open block.profile
f = open('block.profile')
blocks = []
for line in f:
  (symbol,offset,count_str) = line.strip().split(',')
  count = int(count_str)
  if count > 2:
    blocks.append(symbol+' '+offset)
f.close()

for block in blocks:
  for run in range(0, runs):
    print 'Speeding up', block, 'run', run
    run_command(program + ' speedup ' + block + ' ' + args, 120)
    print 'Causaling', block, 'run', run
    run_command(program + ' causal ' + block + ' ' + args, 120)
