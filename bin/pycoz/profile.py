import copy
import sys
import traceback

# Parameters of a speedup experiment, not including the measured progress rate(s)
class experiment:
  def __init__(self, selected, speedup, duration, samples):
    self.selected = selected
    self.speedup = speedup
    self.duration = duration
    self.samples = samples

# Measurement of a single progress point's rate during an experiment
class measurement:
  def __init__(self, name, typ, delta, experiment):
    self.name = name
    self.typ = typ
    self.delta = delta
    self.selected = experiment.selected
    self.speedup = experiment.speedup
    self.duration = experiment.duration
    self.samples = experiment.samples
  
  def period(self):
    return float(self.duration) / float(self.delta)
  
  def __iadd__(self, other):
    assert self != other
    assert self.name == other.name
    assert self.typ == other.typ
    assert self.selected == other.selected
    assert self.speedup == other.speedup
    
    self.delta += other.delta
    self.duration += other.duration
    self.samples += other.samples
    
    return self

class profile:
  def __init__(self):
    # Map from speedup location -> progress point -> speedup amount -> measurement
    self.measurements = {}
    # Map of total sample counts at each location
    self.sample_counts = {}
    # Total runtime
    self.runtime = 0
  
  # Remove measurements with zero total delta (no progress) or fewer than `min_speedups` distinct speedup measurements
  def prune(self, min_speedups=1):
    for selected in self.measurements.keys():
      for progress_point in self.measurements[selected].keys():
        for speedup in self.measurements[selected][progress_point].keys():
          
          # If total delta is zero, delete the speedup entry
          if self.measurements[selected][progress_point][speedup].delta == 0:
            del self.measurements[selected][progress_point][speedup]
            
        # If there is no baseline measurement (speedup == 0) drop the progress point
        if 0.0 not in self.measurements[selected][progress_point]:
          del self.measurements[selected][progress_point]
          
        # If there are too few distinct speedups, drop the progress point
        elif len(self.measurements[selected][progress_point]) < min_speedups:
          del self.measurements[selected][progress_point]
          
      # If all progress points were dropped, remove the selected location
      if len(self.measurements[selected]) == 0:
        del self.measurements[selected]
  
  def add_measurement(self, m):
    if m.selected not in self.measurements:
      self.measurements[m.selected] = {}
    if m.name not in self.measurements[m.selected]:
      self.measurements[m.selected][m.name] = {}
    if m.speedup not in self.measurements[m.selected][m.name]:
      self.measurements[m.selected][m.name][m.speedup] = m
    else:
      self.measurements[m.selected][m.name][m.speedup] += m
  
  def set_runtime(self, runtime):
    self.runtime = runtime
  
  def set_samples(self, location, count):
    self.sample_counts[location] = count
  
  def empty(self):
    return len(self.measurements) == 0
  
  def get_columns(self):
    return ('Location', 'Progress Point', 'Speedup', 'Progress Period', 'Progress Speedup')
  
  def to_records(self, abbrv_locations=True):
    result = []
    
    for (selected, progress_points) in self.measurements.items():
      print_location = selected
      if abbrv_locations:
        print_location = shorten_location(selected)
      
      for (progress_point, speedups) in progress_points.items():
        baseline_period = None
        if 0.0 in speedups and speedups[0.0].delta > 0:
          baseline_period = speedups[0.0].period()
        
        for speedup in sorted(speedups.keys()):
          measurement = speedups[speedup]
          if measurement.delta > 0:
            progress_speedup = None
            if baseline_period:
              # Compute the percent speedup in the progress period
              progress_speedup = 1.0 - measurement.period() / baseline_period
              
              # Apply the phase correction to the speedup if there are any samples in the selected location
              if measurement.samples > 0:
                progress_speedup *= measurement.duration
                progress_speedup /= measurement.samples
                progress_speedup *= self.sample_counts[selected]
                progress_speedup /= self.runtime
            
            result.append((print_location, progress_point, speedup, measurement.period(), progress_speedup))
            
    return result
  
  def to_csv(self, abbrv_locations=True):
    result = ''
    result += ','.join(self.get_columns())
    
    for record in self.to_records():
      result += ','.join(map(str, record)) + '\n'
            
    return result
  
  # Merge profiles
  def __iadd__(self, other):
    assert self != other
    
    # Loop over measured progress rates to combine results
    for (selected, progress_points) in other.measurements.items():
      if selected not in self.measurements:
        self.measurements[selected] = progress_points
      else:
        for (progress_point, speedups) in progress_points.items():
          if progress_point not in self.measurements[selected]:
            self.measurements[selected][progress_point] = speedups
          else:
            for (speedup, m) in speedups.items():
              if speedup not in self.measurements[selected][progress_point]:
                self.measurements[selected][progress_point][speedup] = m
              else:
                self.measurements[selected][progress_point][speedup] += m
    
    # Add total sample counts
    for (location, count) in other.sample_counts.items():
      if location in self.sample_counts:
        self.sample_counts[location] += count
      else:
        self.sample_counts[location] = count
    
    # Add total runtime
    self.runtime += other.runtime
    
    return self

def read_profile(filename):
  result = profile()
  
  current_run = None    # Profile for just the current run
  
  try:
    f = open(filename)
    
    # The last logged experiment
    current_exp = None
    
    # Experiment results that do not yet have sample counts for phase correction
    buffered_profile = profile()
    
    # Loop over lines in the profile
    for line in f:
      # Decode the command prefix and tab-separated key=value pairs
      (command, data) = parse_line(line)
      
      if command == 'startup':
        # If there is a profile from a prior run, flush it to the combined profile
        if current_run:
          result += current_run
        
        # Start a new profile for the current run
        current_run = profile()
        
        # No active experiment
        current_exp = None
        
      elif command == 'shutdown':
        # No active experiment
        current_exp = None
      
      elif command == 'experiment':
        # Set the current experiment
        selected = data['selected']
        speedup = float(data['speedup'])
        duration = int(data['duration'])
        samples = int(data['selected-samples'])
        
        current_exp = experiment(selected, speedup, duration, samples)
      
      elif command == 'progress-point':
        # Log a performance change for a single progress point during the current experiment
        if current_exp:
          m = measurement(data['name'], data['type'], int(data['delta']), current_exp)
          buffered_profile.add_measurement(m)
      
      elif command == 'samples':
        # Flush the buffered profile to the current run profile
        if not buffered_profile.empty():
          current_run += buffered_profile
          buffered_profile = profile()
        
        # Update sample counts for the current run
        current_run.set_samples(data['location'], int(data['count']))
        
        # No active experiment
        current_exp = None
      
      elif command == 'runtime':
        current_run.set_runtime(int(data['time']))
      
      else:
        print >> sys.stderr, 'Ignoring unknown profiler line "' + command + '"'
      
    f.close()
    
  except Exception as e:
    # Print the error before returning the valid part of the profile
    print >> sys.stderr, e
    print >> sys.stderr, traceback.format_exc()
  
  # Flush the last run to the combined profile
  if current_run:
    result += current_run
  
  return result

def shorten_location(name):
  if name.find('/') != -1:
    (path, filename) = name.rsplit('/', 1)
    return filename
  else:
    return name

def parse_line(line):
  (command, data_str) = line.strip().split("\t", 1)
  values = {}
  for element in data_str.split("\t"):
    (key, value) = element.split("=", 1)
    values[key] = value
  return (command, values)
