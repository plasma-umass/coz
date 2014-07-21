#!/usr/bin/env python

import time
import subprocess
import sys

slowdown_size = 20
delay_size = 20
runs = 5
program = sys.argv[1]
args = " ".join(sys.argv[2:])

def run_command(command, timelimit=0):
  p = subprocess.Popen(command+" > /dev/null", shell=True)
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
run_command('rm -f blocks.profile versions.profile')

# Collect baseline run data
for run in range(0, runs):
  print 'Doing baseline run', run
  run_command(program + ' clean ' + args, 60)

# Open block.profile
f = open('blocks.profile')
blocks = []
for line in f:
  (symbol,offset) = line.strip().split('+')
  name = symbol + ' ' + offset
  if name not in blocks:
    blocks.append(symbol+' '+offset)
f.close()

for block in blocks:
  print 'Collecting legacy profile for block', block
  for run in range(0, runs):
    print '  run', run
    run_command(' '.join([program, 'legacy', block, str(slowdown_size), args]), 60)
  
  print 'Collecting causal profile for block', block
  for run in range(0, runs):
    print '  run', run
    run_command(' '.join([program, 'causal', block, str(slowdown_size), str(delay_size), args]), 60)
