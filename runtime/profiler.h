#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <cstdint>
#include <string>

#include "basic_block.h"
#include "causal.h"
#include "counter.h"

enum {
  SampleSignal = 42,
  SamplePeriod = 3000000, // 3 milliseconds
  MaxRoundSamples = 1000
};

void profilerInit(int& argc, char**& argv);
void profilerShutdown();
void registerCounter(Counter* c);
void threadInit();
void threadShutdown();

#endif
