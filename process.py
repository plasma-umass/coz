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
  # Map from line name -> line speedup -> counter name -> list of (counter delta, time) pairs
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
      counter_values = {}
      
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
      speedup_line = data['line']
      (i, counter_start_values) = readCounters(lines, i)
      
    elif command == 'end-speedup':
      phase_time = int(data['time']) - phase_start_time
      delay_count = int(data['delays'])
      delay_size = int(data['delay-size'])
      # Adjust to effective time
      phase_time -= delay_count * delay_size
      
      if speedup_line not in speedup_rates:
        speedup_rates[speedup_line] = {}
      
      if delay_size not in speedup_rates[speedup_line]:
        speedup_rates[speedup_line][delay_size] = {}
      
      (i, counter_end_values) = readCounters(lines, i)
      
      for counter in counter_start_values:
        if counter in counter_end_values:
          difference = counter_end_values[counter] - counter_start_values[counter]
          if difference > 0:
            if counter not in speedup_rates[speedup_line][delay_size]:
              speedup_rates[speedup_line][delay_size][counter] = []
            speedup_rates[speedup_line][delay_size][counter].append((difference, phase_time))
  
  print "line\tline_speedup\tcounter\tcounter_speedup\tbaseline_period\tspeedup_period"
  
  for line in speedup_rates:
    print_line = line
    # If the file name starts with the current dir, remove it (plus the slash)
    if line.startswith(os.getcwd()):
      print_line = line[(len(os.getcwd())+1):]
    
    for delay_size in speedup_rates[line]:
      for counter in speedup_rates[line][delay_size]:
        if counter in baseline_rates:
          baseline_period = avgPeriod(baseline_rates[counter])
          speedup_period = avgPeriod(speedup_rates[line][delay_size][counter])
          
          line_speedup = float(delay_size) / period
          #counter_speedup = baseline_period / speedup_period
          counter_speedup = 1 - speedup_period / baseline_period
          
          print "\t".join([print_line, str(line_speedup), counter, str(counter_speedup), str(baseline_period), str(speedup_period)])

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
