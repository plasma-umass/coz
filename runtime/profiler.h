#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <cstdint>
#include <map>
#include <string>

#include "basic_block.h"
#include "causal.h"
#include "counter.h"

enum {
  PauseSignal = 42,
  SamplePeriod = 1000000, // 1ms
  SampleWakeupCount = 1,
  MinRoundSamples = 1000
};

namespace profiler {
  void registerCounter(Counter* c);
  void shutdown();
  void startup(std::string, std::map<basic_block*, std::string>, basic_block*);
  void threadShutdown();
  void threadStartup();
};

#endif
