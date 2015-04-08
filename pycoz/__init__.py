import argparse
import os
import sys

import coz_plot
import coz_process
import coz_run

# Build the parser
_parser = argparse.ArgumentParser()
_subparsers = _parser.add_subparsers()

coz_plot.build_subparser(_subparsers)
coz_process.build_subparser(_subparsers)
coz_run.build_subparser(_subparsers)

def run_command_line():
  # By default, parse all arguments
  parsed_args = sys.argv[1:]
  remaining_args = []
  # If there is a '---' separator, only parse arguments before the separator
  if '---' in sys.argv:
    separator_index = sys.argv.index('---')
    parsed_args = sys.argv[1:separator_index]
    remaining_args = sys.argv[separator_index+1:]
  # Pass the un-parsed arguments to the parser result
  _parser.set_defaults(remaining_args=remaining_args)
  # Parse it
  args = _parser.parse_args(parsed_args)
  # Call the parser's handler (set by the subcommand parser using defaults)
  args.func(args)