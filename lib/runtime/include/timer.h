#if !defined(CAUSAL_RUNTIME_TIMER_H)
#define CAUSAL_RUNTIME_TIMER_H

#include <time.h>

#include "log.h"
#include "util.h"

class timer {
public:
  timer(int sig) {
    struct sigevent ev = {
      .sigev_notify = SIGEV_THREAD_ID,
      .sigev_signo = sig,
      ._sigev_un = {
        ._tid = gettid()
      }
    };
    
    REQUIRE(timer_create(CLOCK_THREAD_CPUTIME_ID, &ev, &_timer) == 0)
        << "Failed to create timer!";
  }
  
  ~timer() {
    REQUIRE(timer_delete(_timer) == 0) << "Failed to delete tiemr!";
  }
  
  void start_interval(size_t time_ns) {
    long ns = time_ns % 1000000000;
    time_t s = (time_ns - ns) / 1000000000;
    
    struct itimerspec ts = {
      .it_interval = {
        .tv_sec = s,
        .tv_nsec = ns
      },
      .it_value = {
        .tv_sec = s,
        .tv_nsec = ns
      }
    };
    
    REQUIRE(timer_settime(_timer, 0, &ts, NULL) == 0) << "Failed to start interval timer";
  }
  
  void start_oneshot(size_t time_ns) {
    long ns = time_ns % 1000000000;
    time_t s = (time_ns - ns) / 1000000000;
    
    struct itimerspec ts = {
      .it_value = {
        .tv_sec = s,
        .tv_nsec = ns
      }
    };
    
    REQUIRE(timer_settime(_timer, 0, &ts, NULL) == 0) << "Failed to start one-shot timer";
  }
  
private:
  timer(const timer&) = delete;
  void operator=(const timer&) = delete;
  
  timer_t _timer;
};

#endif
