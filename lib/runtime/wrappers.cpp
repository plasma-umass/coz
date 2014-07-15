#include <pthread.h>
#include <signal.h>

#include "real.h"
#include "profiler.h"

/// Type of a thread entry function
typedef void* (*thread_fn_t)(void*);

/**
 * Struct to call the real thread function and pass the given argument
 */
struct thread_wrapper {
private:
  thread_fn_t _fn;
  void* _arg;
  size_t _round;
  size_t _delays;
public:
  thread_wrapper(thread_fn_t fn, void* arg, size_t round, size_t delays) :
      _fn(fn), _arg(arg), _round(round), _delays(delays) {}
  void* run() { return _fn(_arg); }
  size_t get_round() { return _round; }
  size_t get_delays() { return _delays; }
};

/**
 * Prevent profiled applications from registering a handler for the profiler's sampling signal 
 */
extern "C" sighandler_t signal(int signum, sighandler_t handler) {
  if(signum == SampleSignal) {
    return NULL;
  } else {
    return real::signal()(signum, handler);
  }
}

/**
 * Prevent profiled applications from registering a handler for the profiler's sampling signal
 */
extern "C" int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
  if(signum == SampleSignal) {
    return 0;
  } else if(act != NULL && sigismember(&act->sa_mask, SampleSignal)) {
    struct sigaction my_act = *act;
    sigdelset(&my_act.sa_mask, SampleSignal);
    return real::sigaction()(signum, &my_act, oldact);
  } else {
    return real::sigaction()(signum, act, oldact);
  }
}

/**
 * Intercept calls to sigprocmask to ensure the sampling signal is left unmasked
 */
extern "C" int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
  if(how == SIG_BLOCK || how == SIG_SETMASK) {
    if(set != NULL && sigismember(set, SampleSignal)) {
      sigset_t myset = *set;
      sigdelset(&myset, SampleSignal);
      return real::sigprocmask()(how, &myset, oldset);
    }
  }
  
  return real::sigprocmask()(how, set, oldset);
}

/**
 * Intercept calls to pthread_sigmask to ensure the pause signal is unmasked
 */
extern "C" int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
  if(how == SIG_BLOCK || how == SIG_SETMASK) {
    if(set != NULL && sigismember(set, SampleSignal)) {
      sigset_t myset = *set;
      sigdelset(&myset, SampleSignal);
      return real::pthread_sigmask()(how, &myset, oldset);
    }
  }
  
  return real::pthread_sigmask()(how, set, oldset);
}

/**
 * Entry point for all new threads
 */
void* thread_entry(void* arg) {
  // Copy the wrapped thread function and argument
  thread_wrapper* wrapper = (thread_wrapper*)arg;
  thread_wrapper local_wrapper = *wrapper;
  // Delete the allocated wrapper object
  delete wrapper;
  // Register this thread with causal
  profiler::get_instance().thread_startup(local_wrapper.get_round(), local_wrapper.get_delays());
  // Run the real thread function
  void* result = local_wrapper.run();
  // Exit
  pthread_exit(result);
}

/**
 * Intercept calls to create threads
 */
extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr, thread_fn_t fn, void* arg) {
  thread_wrapper* arg_wrapper = new thread_wrapper(fn, arg,
      profiler::get_instance().get_local_round(),
      profiler::get_instance().get_local_delays());
  int result = real::pthread_create()(thread, attr, thread_entry, arg_wrapper);
  return result;
}

/**
 * Intercept all thread exits
 */
extern "C" void pthread_exit(void* result) {
	profiler::get_instance().thread_shutdown();
	real::pthread_exit()(result);
}

/**
 * Intercept calls to exit() to ensure shutdown() is run first
 */
extern "C" void exit(int status) {
  profiler::get_instance().shutdown();
  real::exit()(status);
}

/**
 * Intercept calls to _exit() to ensure shutdown() is run first
 */
extern "C" void _exit(int status) {
  profiler::get_instance().shutdown();
	real::_exit()(status);
}

/**
 * Intercept calls to _Exit() to ensure shutdown() is run first
 */
extern "C" void _Exit(int status) {
  profiler::get_instance().shutdown();
  real::_Exit()(status);
}
