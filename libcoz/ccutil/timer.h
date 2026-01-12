#if !defined(CCUTIL_TIMER_H)
#define CCUTIL_TIMER_H

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

#ifndef __APPLE__
// Linux-specific timer using POSIX timer API
class timer {
public:
  timer() : _initialized(false) {}

  timer(int sig) {
    struct sigevent ev;
    memset(&ev, 0, sizeof(ev));
    ev.sigev_notify = SIGEV_THREAD_ID;
    ev.sigev_signo = sig;
    ev._sigev_un._tid = gettid();

    REQUIRE(timer_create(CLOCK_THREAD_CPUTIME_ID, &ev, &_timer) == 0)
        << "Failed to create timer!";
    
    _initialized = true;
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

#else
// macOS timer using setitimer with ITIMER_PROF
#include <sys/time.h>
#include <signal.h>
#include <cstdio>

// Debug logging for Mac timer investigation
#define COZ_TIMER_DEBUG 0

class timer {
public:
  timer() : _initialized(false), _sig(0) {}
  timer(int sig) : _initialized(true), _sig(sig) {
    // Store the signal for later use
    // On macOS, SIGPROF is delivered when ITIMER_PROF fires
#if COZ_TIMER_DEBUG
    fprintf(stderr, "[COZ_DEBUG_TIMER] timer created with signal %d\n", sig);
#endif
  }

  // Allow move construction and assignment
  timer(timer&& other) : _sig(other._sig), _initialized(other._initialized) {
    other._initialized = false;
  }

  timer& operator=(timer&& other) {
    if (this != &other) {
      _sig = other._sig;
      _initialized = other._initialized;
      other._initialized = false;
    }
    return *this;
  }

  ~timer() {
    if (_initialized) {
      // Stop the timer
      struct itimerval it;
      memset(&it, 0, sizeof(it));
      setitimer(ITIMER_PROF, &it, nullptr);
    }
  }

  void start_interval(size_t time_ns) {
    ASSERT(_initialized) << "Can't start an uninitialized timer";

    // Convert nanoseconds to microseconds
    size_t time_us = time_ns / 1000;
    if (time_us < 1000) time_us = 1000; // Minimum 1ms

    struct itimerval it;
    it.it_value.tv_sec = time_us / 1000000;
    it.it_value.tv_usec = time_us % 1000000;
    it.it_interval.tv_sec = time_us / 1000000;
    it.it_interval.tv_usec = time_us % 1000000;

#if COZ_TIMER_DEBUG
    fprintf(stderr, "[COZ_DEBUG_TIMER] start_interval: time_ns=%zu, time_us=%zu, interval=%ld.%06d sec\n",
            time_ns, time_us, (long)it.it_interval.tv_sec, (int)it.it_interval.tv_usec);
#endif

    REQUIRE(setitimer(ITIMER_PROF, &it, nullptr) == 0) << "Failed to start interval timer";
  }

  void start_oneshot(size_t time_ns) {
    ASSERT(_initialized) << "Can't start an uninitialized timer";

    size_t time_us = time_ns / 1000;
    if (time_us < 1000) time_us = 1000;

    struct itimerval it;
    memset(&it, 0, sizeof(it));
    it.it_value.tv_sec = time_us / 1000000;
    it.it_value.tv_usec = time_us % 1000000;

    REQUIRE(setitimer(ITIMER_PROF, &it, nullptr) == 0) << "Failed to start one-shot timer";
  }

private:
  timer(const timer&) = delete;
  timer& operator=(const timer&) = delete;

  int _sig;
  bool _initialized;
};
#endif

#endif // CCUTIL_TIMER_H
