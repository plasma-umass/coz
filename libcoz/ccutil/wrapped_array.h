#if !defined(CCUTIL_WRAPPED_ARRAY_H)
#define CCUTIL_WRAPPED_ARRAY_H

namespace ccutil {
  template<class T> class wrapped_array {
  private:
    T* _base;
    size_t _size;
  public:
    // Construct an array wrapper from a base pointer and array size
    wrapped_array(T* base, size_t size) : _base(base), _size(size) {}
    wrapped_array(const wrapped_array& other) : _base(other._base), _size(other._size) {}
  
    // Get the size of the wrapped array
    size_t size() { return _size; }
  
    // Access an element by index
    T& operator[](size_t i) { return _base[i]; }
  
    // Get a slice of this array, from a start index (inclusive) to end index (exclusive)
    wrapped_array<T> slice(size_t start, size_t end) {
      return wrapped_array<T>(&_base[start], end - start);
    }
    
    operator T*() {
      return _base;
    }
  
    // Iterator class for convenient range-based for loop support
    class iterator {
    private:
      T* _p;
    public:
      // Start the iterator at a given pointer
      iterator(T* p) : _p(p) {}
    
      // Advance to the next element
      void operator++() { ++_p; }
      void operator++(int) { _p++; }
    
      // Get the current element
      T& operator*() const { return *_p; }
    
      // Compare iterators
      bool operator==(const iterator& other) const { return _p == other._p; }
      bool operator!=(const iterator& other) const { return _p != other._p; }
    };
  
    // Get an iterator positioned at the beginning of the wrapped array
    iterator begin() { return iterator(_base); }
  
    // Get an iterator positioned at the end of the wrapped array
    iterator end() { return iterator(&_base[_size]); }
  };

  // Function for automatic template argument deduction
  template<class A> wrapped_array<A> wrap_array(A* base, size_t size) {
    return wrapped_array<A>(base, size);
  }
}

#endif
