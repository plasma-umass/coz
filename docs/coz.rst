=====
 coz
=====

--------------------------------------------------
profiler running experiments on multithreaded code
--------------------------------------------------

:Author: Emery Berger - emery@cs.umass.edu
:Author: Charlie Curtsinger - curtsinger@grinnell.edu
:Date:   2017-08-06
:Copyright: public domain
:Version: 0.2
:Manual section: 1
:Manual group: User Commands

SYNOPSIS
========

coz run [profiling options] --- <command> [args]

coz plot [-h]

DESCRIPTION
===========

Coz is a new kind of profiler that unlocks optimization opportunities
missed by traditional profilers. Coz employs a novel technique we call
*causal profiling* that measures optimization potential.  This
measurement matches developers' assumptions about profilers: that
optimizing highly-ranked code will have the greatest impact on
performance. Causal profiling measures optimization potential for
serial, parallel, and asynchronous programs without instrumentation of
special handling for library calls and concurrency
primitives. Instead, a causal profiler uses performance experiments to
predict the effect of optimizations. This allows the profiler to
establish causality: "optimizing function X will have effect Y,"
exactly the measurement developers had assumed they were getting all
along.

OPTIONS
=======
-h, --help
  show this help message and exit

--binary-scope <file pattern>, -b <file pattern>
  Profile matching executables. Use '%' as a wildcard, or 'MAIN' to
  include the main executable (default=MAIN)

--source-scope <file pattern>, -s <file pattern>
  Profile matching source files. Use '%' as a wildcard.  (default=%)

--progress <source file>:<line number>, -p <source file>:<line number>
  [NOT SUPPORTED] Add a sampling-based progress point

--output <profile output>, -o <profile output>
  Profiler output (default=`profile.coz`)

--end-to-end
  Run a single performance experiment per-execution

--fixed-line <source file>:<line number>
  Evaluate optimizations of a specific source line

--fixed-speedup <speedup> (0-100)
  Evaluate optimizations of a specific amount

SEE ALSO
========

* `<http://coz-profiler.org>`__
* ``man gdb``
