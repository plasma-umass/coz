import copy
import os
import subprocess
import sys

from os.path import abspath, curdir, dirname, sep as path_sep

# Special format handler for line reference arguments
def line_ref(val):
  try:
    (filename, line) = val.rsplit(':', 1)
    line = int(line)
    return filename + ':' + str(line)
  except:
    msg = "Invalid line reference %r. The format is <source file>:<line number>." % val
    raise argparse.ArgumentTypeError(msg)

def build_subparser(subparsers):
  # Build the parser for the `run` sub-command
  parser = subparsers.add_parser('run',
                                     usage='%(prog)s [profiling options] --- <command> [args]',
                                     help='Run a program with coz to collect a causal profile.')

  # Add common profiler options
  parser.add_argument('--binary-scope', '-b',
                      metavar='<file pattern>',
                      default=[], action='append',
                      help='Profile matching executables. The \'%%\' as a wildcard, or \'MAIN\' to include the main executable (default=MAIN)')

  parser.add_argument('--source-scope', '-s',
                      metavar='<file pattern>',
                      default=[], action='append',
                      help='Profile matching source files. Use \'%%\' as a wildcard. (default=%%)')

  parser.add_argument('--progress', '-p',
                      metavar='<source file>:<line number>',
                      type=line_ref, action='append', default=[],
                      help='Add a sampling-based progress point')

  parser.add_argument('--output', '-o',
                      metavar='<profile output>',
                      default=abspath(curdir+path_sep+'profile.coz'),
                      help='Profiler output (default=`profile.coz`)')

  parser.add_argument('--end-to-end',
                      action='store_true', default=False,
                      help='Run a single performance experiment per-execution')

  parser.add_argument('--sample-only',
                      action='store_true', default=False,
                      help='Collect a legacy sampling-only profile.')

  parser.add_argument('--fixed-line',
                      metavar='<source file>:<line number>', default=None,
                      help='Evaluate optimizations of a specific source line')

  parser.add_argument('--fixed-speedup',
                      metavar='<speedup> (0-100)',
                      type=int, choices=range(0, 101), default=None,
                      help='Evaluate optimizations of a specific amount')

  # Use defaults to recover handler function and parser object from parser output
  parser.set_defaults(func=run, parser=parser)

def run(args):
  # Ensure the user specified a command after the '---' separator
  if len(args.remaining_args) == 0:
    print 'error: specify a command to profile after `---`\n'
    args.parser.print_help()
    exit(1)

  env = copy.deepcopy(os.environ)
  coz_prefix = dirname(dirname(abspath(sys.argv[0])))
  coz_runtime = coz_prefix + path_sep + 'coz' + path_sep + 'libcoz' + path_sep + 'libcoz.so'

  if 'LD_PRELOAD' in env:
    env['LD_PRELOAD'] += ':' + coz_runtime
  else:
    env['LD_PRELOAD'] = coz_runtime

  if len(args.binary_scope) > 0:
    env['COZ_BINARY_SCOPE'] = '\t'.join(args.binary_scope)
  else:
    env['COZ_BINARY_SCOPE'] = 'MAIN'

  if len(args.source_scope) > 0:
    env['COZ_SOURCE_SCOPE'] = '\t'.join(args.source_scope)
  else:
    env['COZ_SOURCE_SCOPE'] = '%'

  env['COZ_PROGRESS_POINTS'] = '\t'.join(args.progress)

  env['COZ_OUTPUT'] = args.output

  if args.end_to_end:
    env['COZ_END_TO_END'] = '1'

  if args.sample_only:
    env['COZ_SAMPLE_ONLY'] = '1'

  if args.fixed_line:
    env['COZ_FIXED_LINE'] = args.fixed_line

  if args.fixed_speedup:
    env['COZ_FIXED_SPEEDUP'] = str(args.fixed_speedup)

  try:
    result = subprocess.call(args.remaining_args, env=env)
  except KeyboardInterrupt:
    # Exit with special control-C return code
    result = 130
    # Add a newline to mimic output when running without coz
    print
  exit(result)
