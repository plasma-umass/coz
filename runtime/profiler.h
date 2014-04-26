#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <cstdint>
#include <string>

#include "basic_block.h"

enum {
  CycleSampleSignal = 42,
  CycleSamplePeriod = 1000000,
  
  SelectionSamples = 100000000,
  DelaySize = 1000
};

void profilerInit(int& argc, char**& argv);
void profilerShutdown();
void registerBasicBlock(basic_block* block);
void registerCounter(int kind, size_t* counter, const char* file, int line);
void registerFunction(function_info* fn);
bool shouldIncludeFile(const std::string& filename);
void threadInit();
void threadShutdown();

#endif
