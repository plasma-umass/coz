#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "causal.h"
#include "real.h"

using namespace std;

/// The real main function, renamed by the LLVM pass
extern "C" int __real_main(int argc, char** argv);

void parseArgs(int& argc, char**& argv);
uintptr_t getAddress(const char* function, int offset);

typedef void* (*thread_fn_t)(void*);

/**
 * Entry point for a program run with causal. Execution mode is determined by
 * arguments, which are then stripped off before being passed to the real main
 * function. Causal options must come first. Options are:
 *   <program name> block_profile ...
 *     This mode collects a basic block profile. Execution time includes a
 *     slowdown in every basic block. Output is written to block.profile.
 *
 *   <program name> time_profile ...
 *     This mode collects an execution time profile with sampling. All blocks
 *     run with an inserted delay. Output is written to time.profile.
 *
 *   <program name> speedup <function> <offset> ...
 *     This mode slows down every basic block except one, indicated by symbol
 *     name and offset. Block identifiers must match output from block_profile.
 *
 *   <program name> causal <function> <offset> ...
 *     This mode creates the impression of a speedup in the indicated block. All
 *     blocks run with an added delay, including the indicated block.
 */
int main(int argc, char** argv) {
  // Initialize the profiler and pass arguments
  Causal::instance().initialize();
  parseArgs(argc, argv);
  // Add the main thread
  Causal::instance().addThread();
  // Run the program
	int result = __real_main(argc, argv);
  // Shut down the profiler
  Causal::instance().shutdown();
	exit(result);
}

/**
 * Parse arguments from the beginning of the command line. Removes profiler
 * arguments from argc and argv before the real main function is called.
 */
void parseArgs(int& argc, char**& argv) {
  if(argc >= 2 && strcmp(argv[1], "block_profile") == 0) {
    Causal::instance().setMode(BlockProfile);
    argc--;
    argv[1] = argv[0];
    argv = &argv[1];
    
  } else if(argc >= 2 && strcmp(argv[1], "time_profile") == 0) {
    Causal::instance().setMode(TimeProfile);
    argc--;
    argv[1] = argv[0];
    argv = &argv[1];
    
  } else if(argc >= 4 && strcmp(argv[1], "speedup") == 0) {
    uintptr_t selected_block = getAddress(argv[2], atoi(argv[3]));
    Causal::instance().setMode(Speedup, selected_block);
    argc -= 3;
    argv[3] = argv[0];
    argv = &argv[3];
    
  } else if(argc >= 4 && strcmp(argv[1], "causal") == 0) {
    uintptr_t selected_block = getAddress(argv[2], atoi(argv[3]));
    Causal::instance().setMode(CausalProfile, selected_block);
    argc -= 3;
    argv[3] = argv[0];
    argv = &argv[3];
  }
}

/**
 * Compute an address for a given symbol and offset
 */
uintptr_t getAddress(const char* symbol, int offset) {
  void* base = dlsym(RTLD_DEFAULT, symbol);
  if(base == NULL) {
    perror("Unable to locate symbol:");
    abort();
  }
  return (uintptr_t)base + offset;
}

/**
 * Struct to call the real thread function and pass the given argument
 */
struct ThreadInit {
private:
  thread_fn_t _fn;
  void* _arg;
public:
  ThreadInit(thread_fn_t fn, void* arg) : _fn(fn), _arg(arg) {}
  void* run() { return _fn(_arg); }
};

extern "C" {
  /**
   * Called from every instrumented basic block
   */
  void __causal_probe() {
    Causal::instance().probe((uintptr_t)__builtin_return_address(0));
  }

  /**
   * Intercept all thread exits
   */
  void pthread_exit(void* result) {
  	Causal::instance().removeThread();
  	Real::pthread_exit()(result);
  }

  /**
  * Entry point for all new threads
  */
  void* thread_wrapper(void* p) {
    // Get the thread initialization info (function and argument)
    ThreadInit* init = (ThreadInit*)p;
    ThreadInit local_init = *init;
    delete init;
    // Notify the profiler of the new thread
    Causal::instance().addThread();
    // Run the real thread function
    void* result = local_init.run();
    // End the thread
    pthread_exit(result);
  }

  /**
   * Intercept calls to create threads
   */
  int pthread_create(pthread_t* thread, const pthread_attr_t* attr, thread_fn_t fn, void* arg) {
    void* arg_wrapper = (void*)new ThreadInit(fn, arg);
    int result = Real::pthread_create()(thread, attr, thread_wrapper, arg_wrapper);
    return result;
  }

  /**
   * Intercept calls to exit() to ensure shutdown() is run first
   */
  void exit(int status) {
    Causal::instance().shutdown();
  	Real::exit()(status);
  }

  /**
   * Intercept calls to _exit() to ensure shutdown() is run first
   */
  void _exit(int status) {
    Causal::instance().shutdown();
  	Real::_exit()(status);
  }

  /**
   * Intercept calls to _Exit() to ensure shutdown() is run first
   */
  void _Exit(int status) {
    Causal::instance().shutdown();
    Real::_Exit()(status);
  }
}
