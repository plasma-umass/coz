#!/usr/bin/env python

import sys

def convert_profile(filename):
  (prog_name, suffix) = filename.split('.', 2)
  f = open(filename)
  lines = map(lambda s: s.strip(), f)
  
  g = open(filename+'.js', 'w')
  print >> g, 'var '+prog_name+'_profile = new Profile(\''+('\\n'.join(lines))+'\')'
  f.close()
  g.close()

map(convert_profile, sys.argv[1:])
