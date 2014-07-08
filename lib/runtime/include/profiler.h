#if !defined(CAUSAL_RUNTIME_PROFILER_H)
#define CAUSAL_RUNTIME_PROFILER_H

#include <cstdint>
#include <string>
#include <vector>

#include "causal.h"
#include "counter.h"

enum {
  PauseSignal = 42,
  SamplePeriod = 2100000, // 1ms
  SampleWakeupCount = 1,
  MinRoundSamples = 1000
};

namespace profiler {
  void include_file(const std::string& filename, uintptr_t load_address);
  void registerCounter(Counter* c);
  void shutdown();
  void startup(const std::string& output_filename,
               const std::vector<std::string>& source_progress_names,
               const std::string& fixed_line_name);
  void threadShutdown();
  void threadStartup();
};

#endif
