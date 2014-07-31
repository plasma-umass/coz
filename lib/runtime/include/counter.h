#if !defined(CAUSAL_RUNTIME_COUNTER_H)
#define CAUSAL_RUNTIME_COUNTER_H

#include <memory>
#include <string>

#include "causal.h"
#include "log.h"
#include "perf.h"
#include "support.h"
#include "util.h"

class counter {
public:
  enum class type {
    progress = PROGRESS_COUNTER,
    begin = BEGIN_COUNTER,
    end = END_COUNTER
  };
  
  counter(type kind, std::string name, const char* impl_name) : 
      _kind(kind), _name(name), _impl_name(impl_name) {
    if(_kind == type::progress) {
      _kind_name = "progress";
    } else if(_kind == type::begin) {
      _kind_name = "begin";
    } else if(_kind == type::end) {
      _kind_name = "end";
    } else {
      _kind_name = "unknown";
    }
  }
  
  virtual ~counter() {}
  
  type get_kind() const {
    return _kind;
  }
  
  const char* get_kind_name() const {
    return _kind_name;
  }
  
  const char* get_impl_name() const {
    return _impl_name;
  }
  
  const std::string& get_name() const {
    return _name;
  }
  
  virtual size_t get_count() const = 0;
  
private:
  counter::type _kind;
  std::string _name;
  const char* _kind_name;
  const char* _impl_name;
};

class source_counter : public counter {
public:
  source_counter(counter::type kind, size_t* var, std::string name) : 
      counter(kind, name, "source"), _var(var) {}
  
  virtual ~source_counter() {}
  
  virtual size_t get_count() const {
    return __atomic_load_n(_var, __ATOMIC_SEQ_CST);
  }
  
private:
  size_t* _var;
  
  enum { CalibrationCount = 1000 };
  
public:
  static size_t calibrate() {
    size_t clean_start_time = get_time();
    volatile size_t n = CalibrationCount;
    while(n > 0) {
      n--;
    }
    size_t clean_time = get_time() - clean_start_time;
    
    size_t perturbed_start_time = get_time();
    n = CalibrationCount;
    while(n > 0) {
      CAUSAL_PROGRESS;
      n--;
    }
    size_t perturbed_time = get_time() - perturbed_start_time;
    
    return (perturbed_time - clean_time) / CalibrationCount;
  }
};

class sampling_counter : public counter {
public:
  sampling_counter(std::string name, std::shared_ptr<causal_support::line> l) : 
    counter(counter::type::progress, name, "sampling"), _line(l) {}
  
  virtual ~sampling_counter() {}
  
  virtual size_t get_count() const {
    fprintf(stderr, "%lu\n", _line->get_samples());
    return _line->get_samples();
  }
  
private:
  std::shared_ptr<causal_support::line> _line;
};

class perf_counter : public counter {
public:
  perf_counter(counter::type kind, uintptr_t address, std::string name) :
      counter(kind, name, "perf"),
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
  
  virtual ~perf_counter() {
    _event.stop();
  }
  
  virtual size_t get_count() const {
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
    size_t clean_start_time = get_time();
    looper(CalibrationCount);
    size_t clean_time = get_time() - clean_start_time;
    
    // Create a breakpoint-based trip counter
    perf_counter ctr(counter::type::progress, (uintptr_t)looper, "calibration");
    
    // Time the same execution with instrumentation in place
    size_t perturbed_start_time = get_time();
    looper(CalibrationCount);
    size_t perturbed_time = get_time() - perturbed_start_time;
  
    // Ensure that trips were counted properly
    REQUIRE(ctr.get_count() == CalibrationCount)
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
