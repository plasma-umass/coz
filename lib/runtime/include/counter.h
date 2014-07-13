#if !defined(CAUSAL_RUNTIME_COUNTER_H)
#define CAUSAL_RUNTIME_COUNTER_H

#include <string>

#include "causal.h"
#include "log.h"
#include "perf.h"
#include "util.h"

enum CounterType {
  ProgressCounter = PROGRESS_COUNTER,
  BeginCounter = BEGIN_COUNTER,
  EndCounter = END_COUNTER
};

class Counter {
public:
  Counter(CounterType kind, std::string name, const char* impl_name) : 
      _kind(kind), _name(name), _impl_name(impl_name) {
    if(_kind == ProgressCounter) {
      _kind_name = "progress";
    } else if(_kind == BeginCounter) {
      _kind_name = "begin";
    } else if(_kind == EndCounter) {
      _kind_name = "end";
    } else {
      _kind_name = "unknown";
    }
  }
  
  virtual ~Counter() {}
  
  CounterType getKind() const {
    return _kind;
  }
  
  const char* getKindName() const {
    return _kind_name;
  }
  
  const char* getImplName() const {
    return _impl_name;
  }
  
  const std::string& getName() const {
    return _name;
  }
  
  virtual size_t getCount() const = 0;
  
private:
  CounterType _kind;
  std::string _name;
  const char* _kind_name;
  const char* _impl_name;
};

class SourceCounter : public Counter {
public:
  SourceCounter(CounterType kind, size_t* counter, std::string name) : 
      Counter(kind, name, "source"), _counter(counter) {}
  
  virtual ~SourceCounter() {}
  
  virtual size_t getCount() const {
    return __atomic_load_n(_counter, __ATOMIC_SEQ_CST);
  }
  
private:
  size_t* _counter;
  
  enum { CalibrationCount = 1000 };
  
public:
  static size_t calibrate() {
    size_t clean_start_time = getTime();
    volatile size_t n = CalibrationCount;
    while(n > 0) {
      n--;
    }
    size_t clean_time = getTime() - clean_start_time;
    
    size_t perturbed_start_time = getTime();
    n = CalibrationCount;
    while(n > 0) {
      CAUSAL_PROGRESS;
      n--;
    }
    size_t perturbed_time = getTime() - perturbed_start_time;
    
    return (perturbed_time - clean_time) / CalibrationCount;
  }
};

class PerfCounter : public Counter {
public:
  PerfCounter(CounterType kind, uintptr_t address, std::string name) :
      Counter(kind, name, "perf"),
      _pe({
        .type = PERF_TYPE_BREAKPOINT,
        .bp_type = HW_BREAKPOINT_X,
        .bp_addr = (uint64_t)address,
        .bp_len = sizeof(long),
        .inherit = 1
      }),
      _event(_pe) {
   
    _event.start();
  }
  
  virtual ~PerfCounter() {
    _event.stop();
  }
  
  virtual size_t getCount() const {
    return _event.get_count();
  }
  
private:
  enum { CalibrationCount = 1000 };
  
  static void looper(int n) {
    if(n > 1) {
      looper(n-1);
    }
  }
  
public:
  static size_t calibrate() {
    // Time an execution without instrumentation
    size_t clean_start_time = getTime();
    looper(CalibrationCount);
    size_t clean_time = getTime() - clean_start_time;
    
    // Create a breakpoint-based trip counter
    PerfCounter ctr(ProgressCounter, (uintptr_t)looper, "calibration");
    
    // Time the same execution with instrumentation in place
    size_t perturbed_start_time = getTime();
    looper(CalibrationCount);
    size_t perturbed_time = getTime() - perturbed_start_time;
  
    // Ensure that trips were counted properly
    REQUIRE(ctr.getCount() == CalibrationCount)
      << "Something bad happened in breakpoint calibration. Maybe the test method was optimized away?";
    
    // Return the computed overhead
    return (perturbed_time - clean_time) / CalibrationCount;
  }
  
private:
  struct perf_event_attr _pe;
  perf_event _event;
  
  static size_t _overhead;
};

#endif
