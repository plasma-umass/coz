#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#include <set>
#include <string>

#include "counter.h"
#include "inspect.h"
#include "log.h"
#include "real.h"
#include "profiler.h"

using namespace std;

/// The type of a main function
typedef int (*main_fn_t)(int, char**, char**);

/// The program's real main function
main_fn_t real_main;

/**
 * Called by the application to register a progress counter
 */
extern "C" void __causal_register_counter(CounterType kind, size_t* counter, const char* name) {
  profiler::registerCounter(new SourceCounter(kind, counter, name));
}

/**
 * Pass the real __libc_start_main this main function, then run the real main
 * function. This allows Causal to shut down when the real main function returns.
 */
int wrapped_main(int argc, char** argv, char** other) {
  profiler::startup(argc, argv);
  int result = real_main(argc, argv, other);
  profiler::shutdown();
  return result;
}

/**
 * Interpose on the call to __libc_start_main to run before libc constructors.
 */
extern "C" int __libc_start_main(main_fn_t main_fn, int argc, char** argv,
    void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  // Find the real __libc_start_main
  auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
  // Save the program's real main function
  real_main = main_fn;
  // Run the real __libc_start_main, but pass in the wrapped main function
  int result = real_libc_start_main(wrapped_main, argc, argv, init, fini, rtld_fini, stack_end);
  
  return result;
}

/**
 * Intercept calls to fork. Child process should run with a clean profile.
 */
// TODO: handle fork

/**
 * Intercept calls to exit() to ensure shutdown() is run first
 */
extern "C" void exit(int status) {
  // Run the profiler shutdown, but only if shutdown hasn't been run already
  profiler::shutdown();
  Real::exit()(status);
}

/**
 * Intercept calls to _exit() to ensure shutdown() is run first
 */
extern "C" void _exit(int status) {
  // Run the profiler shutdown, but only if shutdown hasn't been run already
  profiler::shutdown();
	Real::_exit()(status);
}

/**
 * Intercept calls to _Exit() to ensure shutdown() is run first
 */
extern "C" void _Exit(int status) {
  // Run the profiler shutdown, but only if shutdown hasn't been run already
  profiler::shutdown();
  Real::_Exit()(status);
}

/**
 * Prevent profiled applications from registering a handler for the profiler's pause signal 
 */
extern "C" sighandler_t signal(int signum, sighandler_t handler) {
  if(signum == PauseSignal) {
    return NULL;
  } else {
    return Real::signal()(signum, handler);
  }
}

/**
 * Prevent profiled applications from registering a handler for the profiler's pause signal
 */
extern "C" int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
  if(signum == PauseSignal) {
    return 0;
  } else if(act != NULL && sigismember(&act->sa_mask, PauseSignal)) {
    struct sigaction my_act = *act;
    sigdelset(&my_act.sa_mask, PauseSignal);
    return Real::sigaction()(signum, &my_act, oldact);
  } else {
    return Real::sigaction()(signum, act, oldact);
  }
}

/**
 * Intercept calls to sigprocmask to ensure the pause signal is left unmasked
 */
extern "C" int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
  if(how == SIG_BLOCK || how == SIG_SETMASK) {
    if(set != NULL && sigismember(set, PauseSignal)) {
      sigset_t myset = *set;
      sigdelset(&myset, PauseSignal);
      return Real::sigprocmask()(how, &myset, oldset);
    }
  }
  
  return Real::sigprocmask()(how, set, oldset);
}

/**
 * Intercept calls to pthread_sigmask to ensure the pause signal is unmasked
 * TODO: fix strange zero argument bug
 */
/*extern "C" int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
  if(how == SIG_BLOCK || how == SIG_SETMASK) {
    if(set != NULL && sigismember(set, PauseSignal)) {
      sigset_t myset = *set;
      sigdelset(&myset, PauseSignal);
      return Real::pthread_sigmask(how, &myset, oldset);
    }
  }
  
  return Real::pthread_sigmask(how, set, oldset);
}*/
