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
  _output_lock.lock();
  fprintf(_f, "startup\ttime=%lu\n", get_time());
  fprintf(_f, "info\tsample-period=%lu\n", sample_period);
  //fprintf(_f, "info\tsource-counter-overhead=%lu\n", SourceCounter::calibrate());
  fprintf(_f, "info\tperf-counter-overhead=%lu\n", PerfCounter::calibrate());
  _output_lock.unlock();

  // Drop all counters, so we don't use any calibration counters during the real execution
  //counters.clear();
}

/**
 * Log profiler shutdown
 */
void output::shutdown() {
  _output_lock.lock();
  fprintf(_f, "shutdown\ttime=%lu\n", get_time());
  _output_lock.unlock();
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
 * Log the beginning of a speedup profiling round
 */
void output::start_round(line* selected) {
  _output_lock.lock();
  // Write out time, selected line, and progress counter values
  fprintf(_f, "start-round\tline=%s:%lu\ttime=%lu\n",
      selected->get_file()->get_name().c_str(), selected->get_line(), get_time());
  write_counters();
  _output_lock.unlock();
}

/**
 * Log the end of a speedup profiling round
 */
void output::end_round(size_t num_delays, size_t delay_size) {
  _output_lock.lock();
  // Write out time, progress counter values, delay count, and total delay
  fprintf(_f, "end-round\tdelays=%lu\tdelay-size=%lu\ttime=%lu\n",
      num_delays, delay_size, get_time());
  write_counters();
  _output_lock.unlock();
}