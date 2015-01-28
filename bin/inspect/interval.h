#if !defined(CAUSAL_RUNTIME_INTERVAL_H)
#define CAUSAL_RUNTIME_INTERVAL_H

#include <algorithm>
#include <array>
#include <tuple>

using std::array;
using std::max;
using std::min;

template<typename T> class interval {
public:
  interval(T base, T limit) : _base(base), _limit(limit) {}
  
  bool empty() {
    return _limit <= _base;
  }
  
  bool contains(T point) {
    return _base <= point && point < _limit;
  }
  
  bool operator> (T point) {
    return _base > point;
  }
  
  bool operator< (T point) {
    return _limit <= point;
  }
  
  bool operator== (interval<T> other) {
    return _base == other._base && _limit == other._limit;
  }
  
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
  T _base;
  T _limit;
};

#endif
