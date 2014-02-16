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

void onTrap(int signum);
void parseArgs(int& argc, char**& argv);
uintptr_t getAddress(const char* function, int offset);

typedef void* (*thread_fn_t)(void*);

atomic<size_t> Causal::_delay_count = ATOMIC_VAR_INIT(0);
__thread size_t Causal::_local_delay_count = 0; 

/**
 * Entry point for a program run with causal. Execution mode is determined by
 * arguments, which are then stripped off before being passed to the real main
 * function. Causal options must come first. Options are:
 *   <program name> clean ...
 *     Runs with no added slowdown. Also produces a blocks.profile file that
 *     lists all basic blocks.
 *
 *   <program name> legacy <function> <offset> <slowdown> ...
 *     This mode collects a profile with basic block counts and time sampling
 *     while also adding <slowdown>ns to each execution of the selected block.
 *
 *   <program name> causal <function> <offset> <slowdown> <causal delay> ...
 *     This mode creates the impression of a speedup in the indicated block. The
 *     selected block runs with the specified <slowdown>ns of added delay.
 */
int main(int argc, char** argv) {
  signal(SIGILL, onTrap);
  
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

void onTrap(int signum) {
  // do nothing
}

/**
 * Parse arguments from the beginning of the command line. Removes profiler
 * arguments from argc and argv before the real main function is called.
 */
void parseArgs(int& argc, char**& argv) {
  if(argc >= 2 && strcmp(argv[1], "clean") == 0) {
    Causal::instance().setMode(Clean);
    argc--;
    argv[1] = argv[0];
    argv = &argv[1];
    
  } else if(argc >= 5 && strcmp(argv[1], "legacy") == 0) {
    uintptr_t selected_block = getAddress(argv[2], atoi(argv[3]));
    size_t slowdown_size = atoi(argv[4]);
    Causal::instance().setMode(LegacyProfile, selected_block);
    Causal::instance().setSlowdownSize(slowdown_size);
    argc -= 4;
    argv[4] = argv[0];
    argv = &argv[4];

  } else if(argc >= 6 && strcmp(argv[1], "causal") == 0) {
    uintptr_t selected_block = getAddress(argv[2], atoi(argv[3]));
    size_t slowdown_size = atoi(argv[4]);
    size_t delay_size = atoi(argv[5]);
    Causal::instance().setMode(CausalProfile, selected_block);
    Causal::instance().setDelaySize(delay_size);
    Causal::instance().setSlowdownSize(slowdown_size);
    argc -= 5;
    argv[5] = argv[0];
    argv = &argv[5];
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
