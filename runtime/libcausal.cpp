#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#include <set>
#include <string>

#include "causal.h"
#include "inspect.h"
#include "log.h"
#include "real.h"
#include "thread_wrapper.h"

using namespace std;

typedef int (*main_fn_t)(int, char**, char**);

set<string> readProfilerScope(int& argc, char**& argv);

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
 * Process all profiler-specific arguments
 */
void readProfilerArgs(int& argc, char**& argv) {
  bool include_main_exe = true;
  
  for(int i = 0; i < argc; i++) {
    int args_to_remove = 0;
    
    string arg(argv[i]);
    if(arg == "--causal-profile") {
      // Add the next argument as a file pattern for the profiler
      Causal::getInstance().addFilePattern(argv[i+1]);
      args_to_remove = 2;
    } else if(arg == "--causal-exclude-main") {
      // Don't include the main executable in the profile
      include_main_exe = false;
      args_to_remove = 1;
    }
    
    if(args_to_remove > 0) {
      // Shift later arguments back `to_remove` spaces in `argv`
      for(int j = i; j < argc - args_to_remove; j++) {
        argv[j] = argv[j + args_to_remove];
      }
      // Overwrite later arguments with NULL
      for(int j = argc - args_to_remove; j < argc; j++) {
        argv[j] = NULL;
      }
      // Update argc
      argc -= args_to_remove;
      // Decrement i, since argv[i] now holds an unprocessed argument
      i--;
    }
  }
  
  // If the main executable hasn't been excluded, include its full path as a pattern
  if(include_main_exe) {
    char* main_exe = realpath(argv[0], NULL);
    Causal::getInstance().addFilePattern(main_exe);
    free(main_exe);
  }
}

/**
 * Interpose on the call to __libc_start_main to run before libc constructors.
 */
extern "C" int __libc_start_main(main_fn_t main_fn, int argc, char** argv,
    void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  
  // Initialize the profiler object
  Causal::getInstance().initialize();
  // Process all profiler-specific arguments
  readProfilerArgs(argc, argv);
  // Find all basic blocks and register them with the profiler
  registerBasicBlocks();
  
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
