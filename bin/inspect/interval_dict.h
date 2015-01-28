#if !defined(CAUSAL_RUNTIME_INTERVAL_DICT_H)
#define CAUSAL_RUNTIME_INTERVAL_DICT_H

#include <algorithm>
#include <limits>
#include <set>

#include "interval.h"

using std::numeric_limits;
using std::set;

template<typename K, typename V> class interval_dict {
public:
  interval_dict() :
    _base(numeric_limits<K>::min()),
    _limit(numeric_limits<K>::max()) {}
  
  set<V> find(K point) {
    if(point < _base) {
      if(_leftChild) return _leftChild->find(point);
      else return set<V>();
    } else if(point >= _limit) {
      if(_rightChild) return _rightChild->find(point);
      else return set<V>();
    } else {
      return _elements;
    }
  }
  
  void insert(K new_base, K new_limit, V new_value) {
    
    
    // Handle any portion of the new interval that is left of this interval
    
    // Handle any portion of the new interval that is right of this interval
    
    // Handle the portion of the new interval overlapping this interval
  }
  
private:
  K _base;
  K _limit;
  interval_dict* _leftChild = nullptr;
  interval_dict* _rightChild = nullptr;
  set<V> _elements;
};

#endif
