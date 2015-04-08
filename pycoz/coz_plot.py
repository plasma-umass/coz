import profile

try:
  import ggplot
  import pandas
except ImportError as e:
  ggplot = None
  pandas = None

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
  if not ggplot:
    print 'Package `ggplot` was not found. Install it with `pip install ggplot`.'
    print 'Alternately, you can produce a CSV with `coz process` and plot it yourself.'
    return
  
  p = profile.read_profile(args.input)
  p.prune()
  data = pandas.DataFrame(p.to_records(), columns=p.get_columns())

  plot = ggplot.ggplot(data, ggplot.aes(x='Speedup', y='Progress Speedup', color='Progress Point'))
  plot += ggplot.geom_point(size=1.5)
  plot += ggplot.facet_wrap('Location')
  
  if args.output:
    ggplot.ggsave(args.output, plot)
  else:
    print plot
