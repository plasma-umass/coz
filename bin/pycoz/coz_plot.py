
def build_subparser(subparsers):
  # Build the parser for the `plot` sub-command
  parser = subparsers.add_parser('plot',
                                 help='Plot the speedup results from one or more causal profiling runs.')
  
  parser.add_argument('--input', '-i',
                      metavar='<input profile>',
                      default='profile.coz',
                      help='Profile to plot (default=`profile.coz`)')
  
  parser.add_argument('--output', '-o',
                      metavar='<output file>',
                      help='Save plot to this file')

  # Use defaults to recover handler function and parser object from parser output
  parser.set_defaults(func=run, parser=parser)

def run(args):
  print 'In coz plot handler'
