#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <cstdint>
#include <string>

#include "basic_block.h"
#include "causal.h"
#include "counter.h"

enum {
  PauseSignal = 42,
  SamplePeriod = 3000000, // 3 milliseconds
  MaxRoundSamples = 1000
};

namespace profiler {
  void startup(int& argc, char**& argv);
  void shutdown();
  void registerCounter(Counter* c);
};

#endif
