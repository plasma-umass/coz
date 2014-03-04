#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#include <set>
#include <string>

#include "causal.h"
#include "inspect.h"
#include "real.h"
#include "thread_wrapper.h"

using namespace std;

typedef int (*main_fn_t)(int, char**, char**);

/// The program's real main function, 
main_fn_t real_main;

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
  Causal::getInstance().addThread();
  // Run the real thread function
  void* result = local_wrapper.run();
  // Exit
  pthread_exit(result);
}

/**
 * Pass the real __libc_start_main this main function, then run the real main
 * function. This allows Causal to shut down when the real main function returns.
 */
int wrapped_main(int argc, char** argv, char** other) {
  int result = real_main(argc, argv, other);
  Causal::getInstance().shutdown();
  return result;
}

/**
 * Get a set of strings to match against /proc/self/maps
 */
set<string> readProfilerScope(int& argc, char**& argv) {
  set<string> result;
  // Just search the main executable for now. Parse args in the future
  result.insert(realpath(argv[0], NULL));
  return result;
}

/**
 * Interpose on the call to __libc_start_main to run before libc constructors.
 */
extern "C" int __libc_start_main(main_fn_t main_fn, int argc, char** argv,
    void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  
  // Initialize the profiler object
  Causal::getInstance().initialize();
  // Get the set of paths to include in the profile
  set<string> scope = readProfilerScope(argc, argv);
  // Find all basic blocks and register them with the profiler
  registerBasicBlocks(scope);
  
  // Find the real __libc_start_main
  auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
  // Save the program's real main function
  real_main = main_fn;
  // Run the real __libc_start_main, but pass in the wrapped main function
  int result = real_libc_start_main(wrapped_main, argc, argv, init, fini, rtld_fini, stack_end);
  
  return result;
}

/**
 * Intercept calls to create threads
 */
extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr, thread_fn_t fn, void* arg) {
  void* arg_wrapper = (void*)new thread_wrapper(fn, arg);
  int result = Real::pthread_create()(thread, attr, thread_entry, arg_wrapper);
  return result;
}

/**
 * Intercept all thread exits
 */
extern "C" void pthread_exit(void* result) {
	Causal::getInstance().removeThread();
	Real::pthread_exit()(result);
}

/**
 * Intercept calls to exit() to ensure shutdown() is run first
 */
extern "C" void exit(int status) {
  Causal::getInstance().shutdown();
  Real::exit()(status);
}

/**
 * Intercept calls to _exit() to ensure shutdown() is run first
 */
extern "C" void _exit(int status) {
  Causal::getInstance().shutdown();
	Real::_exit()(status);
}

/**
 * Intercept calls to _Exit() to ensure shutdown() is run first
 */
extern "C" void _Exit(int status) {
  Causal::getInstance().shutdown();
  Real::_Exit()(status);
}
