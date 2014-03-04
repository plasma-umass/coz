#if !defined(CAUSAL_RUNTIME_CAUSAL_H)
#define CAUSAL_RUNTIME_CAUSAL_H

#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <map>
#include <new>

#include "basic_block.h"
#include "interval.h"
#include "perf.h"
#include "util.h"

enum {
  SamplingSignal = 42,
  SamplingPeriod = 10000000,
  DelaySize = 1000
};

/// Possible execution modes
enum ProfilerMode {
  BlockProfile,
  TimeProfile,
  Speedup,
  CausalProfile
};

class Causal {
public:
  void initialize() {
    // Record the time at the beginning of execution
    _start_time = getTime();
    // Set up the sampling signal handler
    signal(SamplingSignal, Causal::sampleSignal);
    
    _initialized.store(true);
  }
  
  void addBlock(interval i, basic_block block) {
    _blocks.insert(std::pair<interval, basic_block>(i, block));
  }
  
  void shutdown() {
    if(_initialized.exchange(false) == true) {
      fprintf(stderr, "Got %lu samples\n", _samples.load());
    }
  }
  
  void addThread() {
    startSampling(SamplingPeriod, 42);
  }
  
  void removeThread() {
    stopSampling();
  }
  
  static Causal& getInstance() {
    static char buf[sizeof(Causal)];
    static Causal* theInstance = new(buf) Causal();
    return *theInstance;
  }
  
private:
  Causal() {}
  
  static void sampleSignal(int signum) {
    getInstance()._samples++;
  }
  
  /// Has the profiler been initialized?
  std::atomic<bool> _initialized = ATOMIC_VAR_INIT(false);
  /// The starting time for the program
  size_t _start_time;
  /// The number of cycle samples
  std::atomic<size_t> _samples = ATOMIC_VAR_INIT(0);
  /// The map of basic blocks
  std::map<interval, basic_block> _blocks;
};

#endif
