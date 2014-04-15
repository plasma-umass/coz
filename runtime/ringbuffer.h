#if !defined(CAUSAL_RUNTIME_RINGBUFFER_H)
#define CAUSAL_RUNTIME_RINGBUFFER_H

#include <cstdint>
#include <cstring>

template<size_t Size> class RingBuffer {
public:
  RingBuffer() {}
  
  RingBuffer(uintptr_t base) : _base(base), _pos(0) {}
  
  template<typename T> T peek() {
    T result;
    size_t rounded_pos = _pos % Size;
    size_t chunk1_max_size = Size - rounded_pos;
    if(sizeof(T) > chunk1_max_size) {
      memcpy(&result, (void*)(_base + rounded_pos), chunk1_max_size);
      memcpy((void*)((uintptr_t)&result + chunk1_max_size), (void*)_base, sizeof(T) - chunk1_max_size);
    } else {
      memcpy(&result, (void*)(_base + rounded_pos), sizeof(T));
    }
    return result;
  }
  
  template<typename T> T take() {
    T result = peek<T>();
    _pos += sizeof(T);
    return result;
  }
  
  void skip(size_t n) {
    _pos += n;
  }
  
private:
  uintptr_t _base;
  size_t _pos;
};

#endif
