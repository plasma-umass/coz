#if !defined(CCUTIL_STATIC_MAP_H)
#define CCUTIL_STATIC_MAP_H

#include <atomic>

#include "log.h"

template<typename K, typename V, K NullKey=0, size_t MapSize=4096>
class static_map {
public:
  V* insert(K key) {
    size_t bucket = get_bucket(key);
    size_t offset = 0;
    while(offset < MapSize) {
      K empty_key = NullKey;
      size_t index = (bucket + offset) % MapSize;
      if(_entries[index]._tag.compare_exchange_weak(empty_key, key)) {
        // Successfully tagged the entry
        return &_entries[index]._value;
      }
      // Advance to the next bucket
      offset++;
    }
    
    // TODO: Could just keep probing until a slot opens, but livelock would be possible...
    WARNING << "Thread state map is full!";
    return nullptr;
  }
  
  V* find(K key) {
    size_t bucket = get_bucket(key);
    size_t offset = 0;
    while(offset < MapSize) {
      size_t index = (bucket + offset) % MapSize;
      if(_entries[index]._tag.load() == key) {
        return &_entries[index]._value;
      }
      // Advance to the next bucket
      offset++;
    }
    
    return nullptr;
  }
  
  void remove(K key) {
    size_t bucket = get_bucket(key);
    size_t offset = 0;
    while(offset < MapSize) {
      size_t index = (bucket + offset) % MapSize;
      if(_entries[index]._tag.load() == key) {
        _entries[index]._tag.store(NullKey);
        return;
      }
      // Advance to the next bucket
      offset++;
    }
  }
  
private:
  size_t get_bucket(K key) {
    // TODO: Support hash function parameter if this class is reused
    return key % MapSize;
  }
  
  struct entry {
    std::atomic<K> _tag;
    V _value;
  };
  
  entry _entries[MapSize];
};

#endif
