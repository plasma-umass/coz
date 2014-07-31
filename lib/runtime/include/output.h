#if !defined(CAUSAL_RUNTIME_OUTPUT_H)
#define CAUSAL_RUNTIME_OUTPUT_H

#include <memory>
#include <unordered_set>
#include <string>

#include "counter.h"
#include "spinlock.h"
#include "support.h"

class output {
public:
  output(std::string filename);
  ~output();
  
  void add_counter(counter* c);
  
  void startup(size_t sample_period);
  void shutdown();
  void start_round(causal_support::line* line);
  void end_round(size_t num_delays, size_t delay_size);
  
private:
  void write_counters();
  
  output() = delete;
  output(const output&) = delete;
  void operator=(const output&) = delete;
  
  FILE* _f = nullptr;
  std::unordered_set<counter*> _counters;
  spinlock _counters_lock;
  spinlock _output_lock;
};

#endif
