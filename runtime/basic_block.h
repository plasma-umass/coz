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
  
  void sample() { _samples++; }
  
  bool observed() { return _samples > 0; }
  
  void printInfo(size_t sample_period, size_t total_samples) {
    size_t positive_samples = _samples.load();
    float percent_time = (float)positive_samples / total_samples;
    
    fprintf(stderr, "Block %s:%lu:\n\tpercent total runtime: %f%%\n",
            getFunction()->getName().c_str(), getIndex(),
            percent_time * 100);
  }
  
private:
  function_info* _fn;
  size_t _index;
  interval _range;
  
  /// Cycle samples *in this block*
  std::atomic<size_t> _samples = ATOMIC_VAR_INIT(0);
};

#endif
