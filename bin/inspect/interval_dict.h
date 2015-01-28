#if !defined(CAUSAL_RUNTIME_INTERVAL_DICT_H)
#define CAUSAL_RUNTIME_INTERVAL_DICT_H

#include <array>
#include <limits>
#include <set>

#include "interval.h"

using std::array;
using std::numeric_limits;
using std::set;

template<typename K, typename V> class interval_dict {
public:
  /// Construct an interval dictionary mapping the full range of key values to an empty set
  interval_dict() : _range(numeric_limits<K>::min(), numeric_limits<K>::max()) {}
  
  /// Find all values associated with intervals containing a given point
  set<V> find(K point) {
    if(point < _range) {
      // Recurse left if there is a left child, otherwise return nothing
      if(_leftChild) return _leftChild->find(point);
      else return set<V>();
    } else if(point > _range) {
      // Recurse right if there is a right child, otherwise return nothing
      if(_rightChild) return _rightChild->find(point);
      else return set<V>();
    } else {
      // Matches this node. Return the set of elements
      return _elements;
    }
  }
  
  void insert(K new_base, K new_limit, V new_value) {
    insert(interval<K>(new_base, new_limit), new_value);
  }
  
  void insert(interval<K> new_range, V new_value) {
    // Split the new interval into portions left, right, and overlapping this node
    array<interval<K>, 3> parts = _range.split(new_range);
    
    // Handle any portion of the new interval that is left of this interval
    if(!parts[0].empty()) {
      // Is there already a left child?
      if(_leftChild) {
        // Yes: recurse
        _leftChild->insert(parts[0], new_value);
      } else {
        // No: create one
        _leftChild = new interval_dict(parts[0]);
        _leftChild->_elements.insert(new_value);
      }
    }
    
    // Handle any portion of the new interval that is right of this interval
    if(!parts[2].empty()) {
      // Is there already a right child?
      if(_rightChild) {
        // Yes: recurse
        _rightChild->insert(parts[2], new_value);
      } else {
        // No: create one
        _rightChild = new interval_dict(parts[2]);
        _rightChild->_elements.insert(new_value);
      }
    }
    
    // Handle the portion of the new interval overlapping this interval
    if(!parts[1].empty()) {
      // Split the current interval by the overlapping portion
      array<interval<K>, 3> self_parts = parts[1].split(_range);
      
      // Shrink the current interval to the part that overlaps the new range
      _range = self_parts[1];
      
      // Move any left-overhang to a left child
      if(!self_parts[0].empty()) {
        // Add a new left child, and make the current left child its child (if any)
        _leftChild = new interval_dict(self_parts[0], _leftChild, nullptr);
        
        // Copy elements to the new child
        for(auto e : _elements) {
          _leftChild->_elements.insert(e);
        }
      }
      
      // Move any right-overhang to a right child
      if(!self_parts[2].empty()) {
        // Add a new right child, and make the current right child its child (if any)
        _rightChild = new interval_dict(self_parts[2], nullptr, _rightChild);
        
        // Copy elements to the new child
        for(auto e : _elements) {
          _rightChild->_elements.insert(e);
        }
      }
      
      // Add the new element
      _elements.insert(new_value);
    }
  }
  
private:
  interval_dict(interval<K> range,
                interval_dict* leftChild=nullptr,
                interval_dict* rightChild=nullptr) : 
    _range(range),
    _leftChild(leftChild),
    _rightChild(rightChild) {}
  
  interval<K> _range;
  interval_dict* _leftChild = nullptr;
  interval_dict* _rightChild = nullptr;
  set<V> _elements;
};

#endif
