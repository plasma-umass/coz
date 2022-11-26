/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#if !defined(CAUSAL_RUNTIME_PROGRESS_POINT_H)
#define CAUSAL_RUNTIME_PROGRESS_POINT_H

#include <memory>
#include <string>

#include "coz.h"

#include "inspect.h"
#include "perf.h"
#include "util.h"

#include "ccutil/log.h"

/// Enum wrapper around defines for progress point types
enum class progress_point_type {
  throughput = COZ_COUNTER_TYPE_THROUGHPUT,
  begin = COZ_COUNTER_TYPE_BEGIN,
  end = COZ_COUNTER_TYPE_END
};

/**
 * A progress point to measure throughput
 */
class throughput_point {
public:
  class saved;
  
  /// Create a throughput progress point with a given name
  throughput_point(const std::string& name) : _name(name), _counter() {}
  
  /// Save the state of this progress point
  saved* save() const {
    return new saved(this);
  }

  /// Add one to the number of visits to this progress point
  void visit(size_t visits=1) {
    __atomic_add_fetch(&_counter.count, visits, __ATOMIC_RELAXED);
  }

  /// Get the number of visits to this progress point
  size_t get_count() const {
    return __atomic_load_n(&_counter.count, __ATOMIC_RELAXED);
  }
  
  /// Get a pointer to the counter struct (used by source progress points)
  coz_counter_t* get_counter_struct() {
    return &_counter;
  }

  /// Get the name of this progress point
  const std::string& get_name() const {
    return _name;
  }

  class saved {
  public:
    saved() {}
  
    /// Save the state of a throughput point
    saved(const throughput_point* origin) : _origin(origin), _start_count(origin->get_count()) {}

    /// Log the change in this progress point since it was saved
    void log(std::ostream& os) const {
      os << "throughput-point\t"
         << "name=" << _origin->get_name() << "\t"
         << "delta=" << get_delta() << "\n";
    }

    size_t get_delta() const {
      return _origin->get_count() - _start_count;
    }

  protected:
    const throughput_point* _origin;
    size_t _start_count;
  };

private:
  const std::string _name;
  coz_counter_t _counter;
};

/**
 * A progress point to measure latency with two counters
 */
class latency_point {
public:
  class saved;
  
  /// Create a latency progress point with a given name
  latency_point(const std::string& name) : _name(name), _begin_counter(), _end_counter() {}
  
  /// Save the state of this progress point
  saved* save() const {
    return new saved(this);
  }

  /// Add one visit to the begin progress point
  void visit_begin(size_t visits=1) {
    __atomic_add_fetch(&_begin_counter.count, visits, __ATOMIC_RELAXED);
  }
  
  /// Add one visit to the end progress point
  void visit_end(size_t visits=1) {
    __atomic_add_fetch(&_end_counter.count, visits, __ATOMIC_RELAXED);
  }

  /// Get the number of visits to the begin progress point
  size_t get_begin_count() const {
    return __atomic_load_n(&_begin_counter.count, __ATOMIC_RELAXED);
  }
  
  /// Get the number of visits to the end progress point
  size_t get_end_count() const {
    return __atomic_load_n(&_end_counter.count, __ATOMIC_RELAXED);
  }
  
  /// Get a pointer to the begin point's counter struct (used by source progress points)
  coz_counter_t* get_begin_counter_struct() {
    return &_begin_counter;
  }
  
  /// Get a pointer to the end point's counter struct (used by source progress points)
  coz_counter_t* get_end_counter_struct() {
    return &_end_counter;
  }

  /// Get the name of this progress point
  const std::string& get_name() const {
    return _name;
  }

  class saved {
  public:
    saved() {}
  
    /// Save the state of a throughput point
    saved(const latency_point* origin) : _origin(origin),
                                         _begin_start_count(origin->get_begin_count()),
                                         _end_start_count(origin->get_end_count()) {}

    /// Log the change in this progress point since it was saved
    virtual void log(std::ostream& os) const {
      os << "latency-point\t"
         << "name=" << _origin->get_name() << "\t"
         << "arrivals=" << get_begin_delta() << "\t"
         << "departures=" << get_end_delta() << "\t"
         << "difference=" << get_difference() << "\n";
    }

    virtual size_t get_begin_delta() const {
      return _origin->get_begin_count() - _begin_start_count;
    }
  
    virtual size_t get_end_delta() const {
      return _origin->get_end_count() - _end_start_count;
    }
  
    virtual size_t get_difference() const {
      return _origin->get_begin_count() - _origin->get_end_count();
    }

  protected:
    const latency_point* _origin;
    size_t _begin_start_count;
    size_t _end_start_count;
  };

private:
  const std::string _name;
  coz_counter_t _begin_counter;
  coz_counter_t _end_counter;
};

#endif
