#if !defined(CAUSAL_RUNTIME_PROGRESS_POINT_H)
#define CAUSAL_RUNTIME_PROGRESS_POINT_H

#include <memory>
#include <string>

#include "causal.h"

#include "causal/inspect.h"
#include "causal/perf.h"
#include "causal/util.h"

#include "ccutil/log.h"

/**
 * Abstract class that represents a progress point of some kind.
 */
class progress_point {
public:
  enum class kind {
    progress = PROGRESS_COUNTER,
    begin = BEGIN_COUNTER,
    end = END_COUNTER  
  };
  
  /**
   * Default implementation of a saved progress point state. This will work for any simple
   * counting-based progress point. Latency tracking will require a custom implementation.
   */
  class saved {
  public:
    /// Save the state of a progress point
    saved(const progress_point* origin) : _origin(origin), _start_count(origin->get_count()) {}
    
    /// Check if the progress point has changed by a minimum threshold
    virtual bool changed(size_t threshold) const {
      return _origin->get_count() - _start_count > threshold;
    }
      
    /// Log the change in this progress point since it was saved
    virtual void log(std::ostream& os) const {
      os << "progress-point\t"
         << "name=" << _origin->get_name() << "\t"
         << "type=" << _origin->get_type() << "\t"
         << "delta=" << (_origin->get_count() - _start_count) << "\n";
    }
    
  protected:
    const progress_point* _origin;
    size_t _start_count;
  };
  
  /// Create a progress point with a given name
  progress_point(const std::string& name) : _name(name) {}
  
  /// Virtual destructor
  virtual ~progress_point() {}
  
  /// Implementation-dependent count access
  virtual size_t get_count() const = 0;
  
  /// Implementation-dependent type
  virtual const std::string get_type() const = 0;
  
  /// Get the name of this progress point
  const std::string& get_name() const {
    return _name;
  }
  
  /// Take a snapshot of the progress point for later logging
  virtual saved* save() const {
    return new saved(this);
  }
  
private:
  const std::string _name;
};

/// Progress point that uses the CAUSAL_PROGRESS macro inserted in the program's source code
class source_progress_point : public progress_point {
public:
  source_progress_point(const std::string& name, size_t* var) : progress_point(name), _var(var) {}
  
  virtual size_t get_count() const {
    return __atomic_load_n(_var, __ATOMIC_RELAXED);
  }
  
  virtual const std::string get_type() const {
    return "source";
  }
  
private:
  size_t* _var;
};

/// Progress point that uses samples in a selected line
class sampling_progress_point : public progress_point {
public:
  sampling_progress_point(const std::string& name, std::shared_ptr<line> l) :
      progress_point(name), _line(l) {}

  virtual size_t get_count() const {
    return _line->get_samples();
  }
  
  virtual const std::string get_type() const {
    return "sampling";
  }
  
private:
  std::shared_ptr<line> _line;
};

/// Progress point that increments one time only, at the end of the execution
class end_progress_point : public progress_point {
public:
  end_progress_point() : progress_point("complete execution"), _count(0) {}
  
  virtual size_t get_count() const {
    return _count;
  }
  
  virtual const std::string get_type() const {
    return "end-to-end";
  }
  
  void done() {
    _count = 1;
  }
  
private:
  size_t _count;
};

/// Progress point that uses a perf_event breakpoint to count trips
class breakpoint_progress_point : public progress_point {
public:
  breakpoint_progress_point(const std::string& name, uintptr_t address) :
      progress_point(name),
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
  
  virtual size_t get_count() const {
    return _event.get_count();
  }
  
  virtual const std::string get_type() const {
    return "breakpoint";
  }
  
private:
  struct perf_event_attr _pe;
  perf_event _event;
};

#endif
