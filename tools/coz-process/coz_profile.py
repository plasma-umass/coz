#!/usr/bin/env python

import os
import sys
import math
import copy
import traceback

class experiment:
  def __init__(self, line, duration, speedup, total_delay, samples):
    self.line = line
    self.duration = float(duration)
    self.speedup = float(speedup)
    self.total_delay = float(total_delay)
    self.samples = samples
    
    self.counter = None
    self.counter_delta = 0
    self.counter_kind = None
    self.counter_impl = None
  
  def with_counter(self, name, delta, impl, kind):
    e = copy.copy(self)
    e.counter = name
    e.counter_delta = float(delta)
    e.counter_impl = impl
    e.counter_kind = kind
    return e
  
  def __iadd__(self, other):
    # Experiments should only be added if they have the same parameters
    assert self.line == other.line
    assert self.speedup == other.speedup
    assert self.counter == other.counter
    assert self.counter_kind == other.counter_kind
    assert self.counter_impl == other.counter_impl
    
    self.duration += other.duration
    self.total_delay += other.total_delay
    self.samples += other.samples
    self.counter_delta += other.counter_delta
    
    return self
  
  def effective_duration(self):
    return self.duration - self.total_delay
    
  def wall_time_duration(self):
    return self.duration
  
  def period(self):
    return self.effective_duration() / self.counter_delta
    
  def valid(self):
    return self.samples > 0 and self.counter_delta > 0
  
  def to_csv(self, samples, total_time, baseline_period):
    counter_speedup = 1.0 - self.period() / baseline_period
    
    # Apply the phase correction to the progress counter speedup
    correction = self.duration / float(self.samples) * samples / float(total_time)
    counter_speedup *= correction
    
    return '\t'.join([shortenLine(self.line), str(self.speedup), shortenLine(self.counter), str(counter_speedup)])

class profile:
  def __init__(self):
    self.total_time = 0       # Total elapsed time
    self.run_count = 0        # Number of runs
    self.experiments = {}     # Map from counter -> line -> speedup -> experiment
    self.sample_counts = {}   # Number of samples in each line
    self.total_samples = 0    # Total number of samples from all runs
    self.run_times = []       # Times for each of the runs
    
  def process_file(self, filename):
    f = open(filename)
    lines = 0
    try:
      run_start_time = 0          # The time the current run was started
      sample_period = 0           # The sampling period
      experiment_start_time = 0   # The time the current experiment started
      selected_line = None        # The selected line for the current experiment
      selected_line_samples = 0   # Samples in the selected line at the start of the experiment
      global_delays = 0           # Delay count at the start of the experiment
      counter_values = {}         # The values of counters at the start of the experiment
  
      exp = None  # The completed experiment, or None if an experiment is started
  
      # Buffered experiments awaiting sample counts before they can be flushed to `experiments`
      buffered = []
  
      while True:
        # Read the next command line and get key-value pairs for all the command properties
        (command, data) = getCommand(f.next())
    
        lines += 1
    
        if command == 'startup':
          run_start_time = int(data['time'])
          sample_period = int(data['sample-period'])
    
        if command == 'shutdown':
          # Add the runtime to total tiem
          self.total_time += int(data['time']) - run_start_time
          self.total_samples += int(data['samples'])
          self.run_count += 1
          self.run_times.append(int(data['time']) - run_start_time)
    
          # Flush all buffered experiments
          for e in buffered:
            if e.counter not in self.experiments:
              self.experiments[e.counter] = {}
            if e.line not in self.experiments[e.counter]:
              self.experiments[e.counter][e.line] = {}
            if e.speedup not in self.experiments[e.counter][e.line]:
              self.experiments[e.counter][e.line][e.speedup] = e
            else:
              self.experiments[e.counter][e.line][e.speedup] += e
      
          # Clear the buffer
          buffered = []
    
        elif command == 'start-experiment':
          exp = None
          counter_values = {}
      
          experiment_start_time = int(data['time'])
          selected_line = data['line']
          selected_line_samples = int(data['selected-line-samples'])
          global_delays = int(data['global-delays'])
      
        elif command == 'end-experiment':
          duration = int(data['time']) - experiment_start_time
          speedup = float(data['delay-size']) / sample_period
          delay_count = int(data['global-delays']) - global_delays
          total_delay = int(data['delay-size']) * delay_count
          samples = int(data['selected-line-samples']) - selected_line_samples
      
          exp = experiment(selected_line, duration, speedup, total_delay, samples)
    
        elif command == 'counter':
          if not exp:
            counter_values[data['name']] = int(data['value'])
          elif data['name'] in counter_values:
            counter_delta = int(data['value']) - counter_values[data['name']]
            buffered.append(exp.with_counter(data['name'], counter_delta, data['impl'], data['kind']))
            if exp.speedup != 0 and exp.total_delay == 0:
              baseline = exp.with_counter(data['name'], counter_delta, data['impl'], data['kind'])
              baseline.speedup = 0
              buffered.append(baseline)
    
        elif command == 'samples':
          # Add samples to the outer sample counts
          line = data['line']
          samples = int(data['count'])
          if line not in self.sample_counts:
            self.sample_counts[line] = samples
          else:
            self.sample_counts[line] += samples
      
    except StopIteration:
      pass
    except Exception, e:
      # Print the error, if it wasn't just end-of-file
      print >> sys.stderr, e
      print >> sys.stderr, traceback.format_exc()
    
    # Close the profile and return lines read
    f.close()
    return lines
  
  def write_csv(self, filename):
    # Print csv output
    csvfile = open(filename, 'w')
    print >> csvfile, 'line\tline_speedup\tcounter\tcounter_speedup'
  
    for (counter, lines) in self.experiments.items():
      for (line, speedups) in lines.items():
        for (speedup, exp) in speedups.items():
          if exp.valid() and 0 in speedups and speedups[0].valid():
            baseline_period = speedups[0].period()
            samples = self.sample_counts[line]
            print >> csvfile, exp.to_csv(samples, self.total_time, baseline_period)
  
    csvfile.close()
  
  def write_lines(self, filename):
    # Print line data
    linefile = open(filename, 'w')
  
    for (line, count) in self.sample_counts.items():
      fraction = float(count) / self.total_samples
      if fraction > 0.01:
        print >> linefile, shortenLine(line) + "\t" + str(fraction)
  
    linefile.close()

def getCommand(line):
  (command, data_str) = line.strip().split("\t", 1)
  return (command, getData(data_str))

def getData(data_str):
  obj = {}
  for element in data_str.split("\t"):
    (key, value) = element.split("=", 1)
    obj[key] = value
  return obj

def shortenLine(name):
  if name.find('/') != -1:
    (path, filename) = name.rsplit('/', 1)
    return filename
  else:
    return name

if __name__ == "__main__":
  p = profile()
  filename = sys.argv[1]
  p.process_file(filename)
  p.write_csv(filename.replace('.log', '')+'.csv')
  p.write_lines(filename.replace('.log', '')+'.lines')
