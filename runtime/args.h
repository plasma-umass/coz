#if !defined(CAUSAL_RUNTIME_ARGS_H)
#define CAUSAL_RUNTIME_ARGS_H

#include <string>
#include <vector>

#include "log.h"

class args {
public:
  args(int argc, char** argv) : _argc(argc), _argv(argv) {
    // Fill the _keep vector with trues - all arguments should be kept by default
    _keep.resize(argc, true);
  }
  
  class iterator {
  public:
    iterator(int pos, args* a) : _pos(pos), _a(*a) {}
    
    void next() {
      do {
        _pos++;
      } while(!done() && !_a._keep[_pos]);
    }
    
    bool done() {
      return _pos >= _a._argc;
    }
    
    std::string get() {
      while(!done() && !_a._keep[_pos]) {
        _pos++;
      }
      return _a._argv[_pos];
    }
    
    void drop() {
      _a._keep[_pos] = false;
    }
    
    std::string take() {
      std::string s = get();
      drop();
      return s;
    }
    
    void operator++() {
      next();
    }
    
    std::string operator*() {
      return get();
    }
    
    bool operator!=(const iterator& other) {
      return _pos < _a._argc;
    }
    
  private:
    int _pos;
    args& _a;
  };
  
  iterator begin() {
    return iterator(0, this);
  }
  
  iterator end() {
    return iterator(-1, this);
  }
  
  int commit(char** output_argv) {
    size_t write_index = 0;
    for(size_t read_index = 0; read_index < _argc; read_index++) {
      if(_keep[read_index]) {
        output_argv[write_index] = _argv[read_index];
        write_index++;
      }
    }
    return write_index;
  }
  
private:
  int _argc;
  char** _argv;
  std::vector<bool> _keep;
};

#endif
