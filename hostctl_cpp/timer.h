#if !defined(CCUTIL_TIMER_H)
#define CCUTIL_TIMER_H

#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>

#include "log.h"

class timer {
public:
  timer() : _initialized(false) {}
  
  timer(int sig) {
    std::cout << "In >> timer" << std::endl;
    struct sigevent ev;
    memset(&ev, 0, sizeof(ev));
    ev.sigev_notify = SIGEV_THREAD_ID;
    ev.sigev_signo = sig;
    ev._sigev_un._tid = gettid();

    REQUIRE(timer_create(CLOCK_THREAD_CPUTIME_ID, &ev, &_timer) == 0)
        << "Failed to create timer!";
    
    _initialized = true;
    std::cout << "Out >> timer" << std::endl;

  }
  
  timer(timer&& other) {
    _timer = other._timer;
    _initialized = other._initialized;
    other._initialized = false;
  }
  
  ~timer() {
    if(_initialized) {
      REQUIRE(timer_delete(_timer) == 0) << "Failed to delete timer!";
    }
  }
  
  void operator=(timer&& other) {
    _timer = other._timer;
    _initialized = other._initialized;
    other._initialized = false;
  }
  
  void start_interval(size_t time_ns) {
    std::cout << "In >> start_interval" << std::endl;    
    ASSERT(_initialized) << "Can't start an uninitialized timer";
    
    long ns = time_ns % 1000000000;
    time_t s = (time_ns - ns) / 1000000000;

    struct itimerspec ts;
    memset(&ts, 0, sizeof(ts));
    ts.it_interval.tv_sec = s;
    ts.it_interval.tv_nsec = ns;
    ts.it_value.tv_sec = s;
    ts.it_value.tv_nsec = ns;

    REQUIRE(timer_settime(_timer, 0, &ts, NULL) == 0) << "Failed to start interval timer";
    
    _initialized = true;
    std::cout << "Out >> start_interval" << std::endl;   

  }
  
  void start_oneshot(size_t time_ns) {
    ASSERT(_initialized) << "Can't start an uninitialized timer";
    
    long ns = time_ns % 1000000000;
    time_t s = (time_ns - ns) / 1000000000;

    struct itimerspec ts;
    memset(&ts, 0, sizeof(ts));
    ts.it_value.tv_sec = s;
    ts.it_value.tv_nsec = ns;

    REQUIRE(timer_settime(_timer, 0, &ts, NULL) == 0) << "Failed to start one-shot timer";
  }
  
private:
  timer(const timer&) = delete;
  void operator=(const timer&) = delete;
  
  timer_t _timer;
  bool _initialized;
};

#endif
