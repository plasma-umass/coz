#if !defined(CCUTIL_LOG_H)
#define CCUTIL_LOG_H

#include <iostream>

namespace ccutil {
  static const char* InfoColor = "\033[01;34m";
  static const char* WarningColor = "\033[01;33m";
  static const char* FatalColor = "\033[01;31m";
  static const char* SourceColor = "\033[34m";
  static const char* EndColor = "\033[0m";

  class logger_base {
  public:
    logger_base indent(size_t n, size_t tab_size = 2) {
      return logger_base();
    }
    
    template<typename T> logger_base  operator<<(T t) {
      return logger_base();
    }
  };

  class logger : public logger_base {
  private:
    bool _done;
    bool _exit;
  public:
    logger(bool exit = false, bool done = true) : _exit(exit), _done(done) {}
  
    logger(logger&& other) {
      _exit = other._exit;
      _done = other._done;
      other._done = false;
    }
  
    ~logger() {
      if(_done) {
        std::cerr << EndColor << "\n";
        if(_exit) abort();
      }
    }
    
    void operator=(logger&& other) {
      _exit = other._exit;
      _done = other._done;
      other._done = false;
    }
    
    logger&& indent(size_t n, size_t tab_size = 2) {
      for(size_t i=0; i<n; i++) {
        for(size_t j=0; j<tab_size; j++) {
          std::cerr << " ";
        }
      }
      return std::move(*this);
    }
  
    template<typename T> logger&& operator<<(T t) {
      std::cerr << t;
      return std::move(*this);
    }
  };
}

#if defined(NDEBUG)
#  define LOG(color, exit) (ccutil::logger(exit) << color)
#  define INFO (ccutil::logger_base())
#  define ASSERT(cond) (ccutil::logger_base())
#else
#  define LOG(color, exit) (ccutil::logger(exit) << ccutil::SourceColor << "[" << __FILE__ << ":" << __LINE__ << "] " << color)
#  define INFO LOG(ccutil::InfoColor, false)
#  define ASSERT(cond) (cond) ? ccutil::logger_base() : FATAL
#endif

#define WARNING LOG(ccutil::WarningColor, false)
#define PREFER(cond) (cond) ? ccutil::logger_base() : WARNING

#define FATAL LOG(ccutil::FatalColor, true)
#define REQUIRE(cond) (cond) ? ccutil::logger_base() : FATAL

#endif