#include "output.h"

#include <cstdio>
#include <memory>
#include <unordered_set>

#include "counter.h"
#include "spinlock.h"
#include "util.h"

using std::shared_ptr;
using std::string;
using std::unordered_set;
using causal_support::line;

output::output(string filename) {
  _f = fopen(filename.c_str(), "a");
  REQUIRE(_f != NULL) << "Failed to open profiler output file: " << filename;
}

output::~output() {
  fclose(_f);
}

void output::add_counter(Counter* c) {
  _counters_lock.lock();
  _counters.insert(c);
  _counters_lock.unlock();
}

/**
 * Log the start of a profile run, along with instrumentation calibration info
 */
void output::startup(size_t sample_period) {
  fprintf(_f, "startup\ttime=%lu\n", getTime());
  fprintf(_f, "info\tsample-period=%lu\n", sample_period);
  //fprintf(_f, "info\tsource-counter-overhead=%lu\n", SourceCounter::calibrate());
  fprintf(_f, "info\tperf-counter-overhead=%lu\n", PerfCounter::calibrate());

  // Drop all counters, so we don't use any calibration counters during the real execution
  //counters.clear();
}

/**
 * Log profiler shutdown
 */
void output::shutdown() {
  fprintf(_f, "shutdown\ttime=%lu\n", getTime());
}

/**
 * Log the values for all known counters
 */
void output::write_counters() {
  for(Counter* c : _counters) {
    fprintf(_f, "counter\tname=%s\tkind=%s\timpl=%s\tvalue=%lu\n",
        c->getName().c_str(), c->getKindName(), c->getImplName(), c->getCount());
  }
}

/**
 * Log the beginning of a baseline profiling round
 */
void output::baseline_start() {
  // Write out time and progress counter values
  fprintf(_f, "start-baseline\ttime=%lu\n", getTime());
  write_counters();
}

/**
 * Log the end of a baseline profiling round
 */
void output::baseline_end() {
  // Write out time and progress counter values
  fprintf(_f, "end-baseline\ttime=%lu\n", getTime());
  write_counters();
}

/**
 * Log the beginning of a speedup profiling round
 */
void output::speedup_start(shared_ptr<line> selected) {
  // Write out time, selected line, and progress counter values
  fprintf(_f, "start-speedup\tline=%s:%lu\ttime=%lu\n",
      selected->get_file()->get_name().c_str(), selected->get_line(), getTime());
  write_counters();
}

/**
 * Log the end of a speedup profiling round
 */
void output::speedup_end(size_t num_delays, size_t delay_size) {
  // Write out time, progress counter values, delay count, and total delay
  fprintf(_f, "end-speedup\tdelays=%lu\tdelay-size=%lu\ttime=%lu\n",
      num_delays, delay_size, getTime());
  write_counters();
}