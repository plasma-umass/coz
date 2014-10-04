import profile

def build_subparser(subparsers):
  # Build the parser for the `process` sub-command
  parser = subparsers.add_parser('process',
                                 help='Process a causal profile to produce a CSV of program speedups.')
  
  parser.add_argument('--input', '-i',
                      metavar='<input profile>', default='profile.coz',
                      help='Profile to process (default=`profile.coz`)')

  parser.add_argument('--output', '-o',
                      metavar='<output>', default='profile.csv',
                      help='Output file (default=`profile.csv`)')

  # Use defaults to recover handler function and parser object from parser output
  parser.set_defaults(func=run, parser=parser)

def run(args):
  p = profile.read_profile(args.input)
  p.prune()
  f = open(args.output, 'w+')
  f.write(p.to_csv())
  f.close()
