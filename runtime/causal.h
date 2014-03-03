#if !defined(CAUSAL_RUNTIME_CAUSAL_H)
#define CAUSAL_RUNTIME_CAUSAL_H

#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <map>
#include <new>

#include "perf.h"
#include "util.h"

enum {
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
  void initialize(int& argc, char**& argv) {
    srand((unsigned int)getTime());
    _start_time = getTime();
    signal(42, Causal::sampleSignal);
    addThread();
    
    _initialized.store(true);
  }
  
  void shutdown() {
    fprintf(stderr, "Got %lu samples\n", _samples.load());
    if(_initialized.exchange(false) == true) {
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
};

#endif
