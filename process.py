#!/usr/bin/env python

import os
import sys
import math

def main(filename):
  total_runtime = 0
  runs = 0
  period = 0
  counter_values = {}
  # Map from counter name -> list of (counter delta, time) pairs
  baseline_rates = {}
  # Map from block name -> block speedup -> counter name -> list of (counter delta, time) pairs
  speedup_rates = {}
  
  f = open(filename)
  lines = [line for line in f]
  f.close()
  
  i = 0
  while i < len(lines):
    (command, data) = getCommand(lines[i])
    i += 1
    
    if command == 'startup':
      start_time = int(data['time'])
      
    elif command == 'shutdown':
      total_runtime += int(data['time']) - start_time
      runs += 1
      
    elif command == 'info':
      if 'sample-period' in data:
        period = int(data['sample-period'])
      
    elif command == 'start-baseline':
      phase_start_time = int(data['time'])
      (i, counter_start_values) = readCounters(lines, i)
      
    elif command == 'end-baseline':
      phase_time = int(data['time']) - phase_start_time
      (i, counter_end_values) = readCounters(lines, i)
      
      for counter in counter_start_values:
        if counter in counter_end_values:
          difference = counter_end_values[counter] - counter_start_values[counter]
          if difference > 0:
            if counter not in baseline_rates:
              baseline_rates[counter] = []
            baseline_rates[counter].append((difference, phase_time))
      
    elif command == 'start-speedup':
      phase_start_time = int(data['time'])
      speedup_block = data['block']
      (i, counter_start_values) = readCounters(lines, i)
      
    elif command == 'end-speedup':
      phase_time = int(data['time']) - phase_start_time
      delay_count = int(data['delays'])
      delay_size = int(data['delay-size'])
      # Adjust to effective time
      phase_time -= delay_count * delay_size
      
      if speedup_block not in speedup_rates:
        speedup_rates[speedup_block] = {}
      
      if delay_size not in speedup_rates[speedup_block]:
        speedup_rates[speedup_block][delay_size] = {}
      
      (i, counter_end_values) = readCounters(lines, i)
      
      for counter in counter_start_values:
        if counter in counter_end_values:
          difference = counter_end_values[counter] - counter_start_values[counter]
          if difference > 0:
            if counter not in speedup_rates[speedup_block][delay_size]:
              speedup_rates[speedup_block][delay_size][counter] = []
            speedup_rates[speedup_block][delay_size][counter].append((difference, phase_time))
  
  print "block\tblock_speedup\tcounter\tcounter_speedup\terr_min\terr_max"
  
  for block in speedup_rates:
    for delay_size in speedup_rates[block]:
      for counter in speedup_rates[block][delay_size]:
        if counter in baseline_rates:
          baseline_period = avgPeriod(baseline_rates[counter])
          speedup_period = avgPeriod(speedup_rates[block][delay_size][counter])
          
          block_speedup = float(delay_size) / period
          counter_speedup = speedup_period / baseline_period
          
          (min_baseline, max_baseline) = periodErrorBounds(baseline_rates[counter])
          (min_speedup, max_speedup) = periodErrorBounds(speedup_rates[block][delay_size][counter])
          
          (err_min, err_max) = (min_speedup / max_baseline, max_speedup / min_baseline)
          
          print "\t".join([block, str(block_speedup), counter, str(counter_speedup), str(err_min), str(err_max)])

def totalDelta(rates):
  total_delta = 0
  for (delta, time) in rates:
    total_delta += delta
  return float(total_delta)

def totalTime(rates):
  total_time = 0
  for (delta, time) in rates:
    total_time += time
  return float(total_time)

def avgPeriod(rates):
  return totalTime(rates) / totalDelta(rates)
  
def periodErrorBounds(rates):
  total_time = totalTime(rates)
  total_delta = totalDelta(rates)
  err = math.sqrt(total_delta)
  if err == 1:
    err = 0
  return (total_time / (total_delta - err), total_time / (total_delta + err))

def readCounters(lines, i):
  counter_values = {}
  (command, data) = getCommand(lines[i])
  while command == 'counter':
    i += 1
    counter_values[data['name']] = int(data['value'])
    (command, data) = getCommand(lines[i])
  return (i, counter_values)

def getCommand(line):
  (command, data_str) = line.strip().split("\t", 1)
  return (command, getData(data_str))

def getData(data_str):
  obj = {}
  for element in data_str.split("\t"):
    (key, value) = element.split("=", 1)
    obj[key] = value
  return obj

main(sys.argv[1])
