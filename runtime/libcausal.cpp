#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#include "causal.h"
#include "real.h"
#include "thread_wrapper.h"

using namespace std;

typedef int (*main_fn_t)(int, char**, char**);

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

main_fn_t real_main;

int wrapped_main(int argc, char** argv, char** other) {
  int result = real_main(argc, argv, other);
  Causal::getInstance().shutdown();
  return result;
}

/**
 * Interpose on the call to __libc_start_main to run before libc constructors.
 */
extern "C" int __libc_start_main(main_fn_t main_fn, int argc, char** argv,
    void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  
  Causal::getInstance().initialize(argc, argv);
  
  auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
  real_main = main_fn;
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
