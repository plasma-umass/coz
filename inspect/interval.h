#if !defined(CAUSAL_RUNTIME_INTERVAL_H)
#define CAUSAL_RUNTIME_INTERVAL_H

#include <algorithm>
#include <array>
#include <tuple>

using std::array;
using std::max;
using std::min;

/// Class that stores an interval of the form [base, limit) (closed left, open right)
template<typename T> class interval {
public:
  /// Create an interval with a given base (inclusive) and limit (exclusive)
  interval(T base, T limit) : _base(base), _limit(limit) {}
  
  /// Does this interval contain any points?
  bool empty() {
    return _limit <= _base;
  }
  
  /// Does this interval contain a specific point?
  bool contains(T point) {
    return _base <= point && point < _limit;
  }
  
  /// Is a point entirely to the right of this interval?
  bool operator> (T point) {
    return _base > point;
  }
  
  /// Is a point entirely to the left of this interval?
  bool operator< (T point) {
    return _limit <= point;
  }
  
  /// Is this interval exactly equal to another interval?
  bool operator== (interval<T> other) {
    return _base == other._base && _limit == other._limit;
  }
  
  /// Break the provided interval into three parts: the portion entirely left of this interval, the portion overlapping this interval, and the portion entirely right of this interval
  array<interval, 3> split(interval other) {
    return {
      // Part of `other` left of `this`
      interval(other._base, min(other._limit, _base)),
      // Part of `other` overlapping `this`
      interval(max(_base, other._base), min(_limit, other._limit)),
      // Part of `other` right of `this`
      interval(max(_limit, other._base), other._limit)
    };
  }
  
private:
  T _base;  //< The base of this interval (inclusive)
  T _limit; //< The limit of this interval (exclusive)
};

/// Operator definition for point < interval
template<typename T> static bool operator<(T p, interval<T> i) {
  return i > p;
}

/// Operator definition for point > interval
template<typename T> static bool operator>(T p, interval<T> i) {
  return i < p;
}

#endif
