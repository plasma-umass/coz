#if !defined(CAUSAL_RUNTIME_BASIC_BLOCK_H)
#define CAUSAL_RUNTIME_BASIC_BLOCK_H

#include <atomic>
#include <string>

#include "interval.h"

class function_info {
public:
  function_info(std::string file, std::string name, std::string demangled, interval loaded) :
    _file(file), _name(name), _demangled(demangled), _loaded(loaded) {}
  
  const std::string& getFileName() const { return _file; }
  const std::string& getRawSymbolName() const { return _name; }
  const std::string& getName() const { return _demangled; }
  const interval& getInterval() const { return _loaded; }

private:
  std::string _file;
  std::string _name;
  std::string _demangled;
  interval _loaded;
};

class basic_block {
public:
  basic_block(function_info* fn, size_t index, interval range) :
    _fn(fn), _index(index), _range(range) {}
  
  const function_info* getFunction() const { return _fn; }
  size_t getIndex() const { return _index; }
  const interval& getInterval() const { return _range; }
  
  void addVisits(long long visits) { _visits += visits; }
  void positiveSample() { _positive_samples++; }
  void negativeSample() { _negative_samples++; }
  
  void printInfo(size_t cycle_period) {
    size_t visits = _visits.load();
    size_t positive_samples = _positive_samples.load();
    size_t total_samples = positive_samples + _negative_samples.load();
    float percent_time = (float)positive_samples / total_samples;
    float single_run_time = (float)positive_samples * (float)cycle_period / (float)visits;
    
    fprintf(stderr, "Block %s:%lu:\n\tvisits: %lu\n\tpercent total runtime: %f%%\n\tsingle runtime: %f cycles\n",
            getFunction()->getName().c_str(), getIndex(),
            visits,
            percent_time * 100,
            single_run_time);
  }
  
private:
  function_info* _fn;
  size_t _index;
  interval _range;
  
  /// Visits to this block during its time as the selected block
  std::atomic<size_t> _visits;
  /// Cycle samples *in this block* during its time as the selected block
  std::atomic<size_t> _positive_samples;
  /// Cycle samples *in some other block* during this block's time as the selected block
  std::atomic<size_t> _negative_samples;
};

#endif
