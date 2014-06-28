#if !defined(CAUSAL_RUNTIME_INTERVAL_H)
#define CAUSAL_RUNTIME_INTERVAL_H

#include <stdint.h>

#include <ios>
#include <iostream>

class interval {
private:
  uintptr_t _base;
  uintptr_t _limit;
  
public:
  /// Standard constructor
  interval(uintptr_t base, uintptr_t limit) : _base(base), _limit(limit) {}
  interval(void* base, void* limit) : interval((uintptr_t)base, (uintptr_t)limit) {}
  
  /// Unit interval constructor
  interval(uintptr_t p) : _base(p), _limit(p+1) {}
  interval(void* p) : interval((uintptr_t)p) {}
  
  /// Default constructor for use in maps
  interval() : interval(nullptr, nullptr) {}
  
  /// Shift
  interval operator+(uintptr_t x) const {
    return interval(_base + x, _limit + x);
  }
  
  /// Shift in place
  void operator+=(uintptr_t x) {
    _base += x;
    _limit += x;
  }
  
  /// Comparison function that treats overlapping intervals as equal
  bool operator<(const interval& b) const {
    return _limit <= b._base;
  }
  
  /// Check if an interval contains a point
  bool contains(uintptr_t x) const {
    return _base <= x && x < _limit;
  }
  
  uintptr_t getBase() const { return _base; }
  uintptr_t getLimit() const { return _limit; }
};

static std::ostream& operator<<(std::ostream& os, const interval& i) {
  os << std::hex << "0x" << i.getBase() << "-0x" << i.getLimit() << std::dec;
  return os;
}

#endif
