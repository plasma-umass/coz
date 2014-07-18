#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#include <set>
#include <string>

#include "counter.h"
#include "log.h"
#include "options.h"
#include "profiler.h"
#include "real.h"
#include "support.h"
#include "util.h"

using namespace std;

/// The type of a main function
typedef int (*main_fn_t)(int, char**, char**);

/// The program's real main function
main_fn_t real_main;

/**
 * Called by the application to register a progress counter
 */
extern "C" void __causal_register_counter(CounterType kind,
                                          size_t* counter,
                                          size_t* backoff,
                                          const char* name) {
  profiler::get_instance().register_counter(new SourceCounter(kind, counter, name));
}

/**
 * Pass the real __libc_start_main this main function, then run the real main
 * function. This allows Causal to shut down when the real main function returns.
 */
int wrapped_main(int argc, char** argv, char** env) {
  // Find the "---" separator between causal arguments and the program name
  size_t causal_argc;
  for(causal_argc = 1; causal_argc < argc && argv[causal_argc] != string("---"); causal_argc++) {
    // Do nothing
  }
  
  // If there is no separator, there must not be any causal arguments
  if(causal_argc == argc) {
    causal_argc = 1;
  }
  
  // Parse the causal command line arguments
  auto args = causal::parse_args(causal_argc, argv);
  
  // Show usage information if the help argument was passed
  if(args.count("help")) {
    causal::show_usage();
    return 1;
  }
  
  // Get the specified file patterns
  vector<string> file_patterns = args["include"].as<vector<string>>();
  
  // If the main executable should NOT be excluded, add the program name to the file patterns set
  if(!args.count("exclude-main")) {
    file_patterns.push_back(argv[causal_argc + 1]);
  }

  // Walk through all the loaded executable images
  for(const auto& file : causal_support::get_loaded_files()) {
    const string& filename = file.first;
    uintptr_t load_address = file.second;
    
    // Exclude libcausal
    if(filename.find("libcausal") == string::npos) {
      // Check if the loaded file matches any of the specified patterns
      for(const string& pat : file_patterns) {
        if(filename.find(pat) != string::npos) {
          INFO << "Processing file " << filename;
          // When a match is found, tell the profiler to include the file
          profiler::get_instance().include_file(filename, load_address);
          break;
        }
      }
    }
  }
  
  // Start the profiler
  profiler::get_instance().startup(args["output"].as<string>(),
                                   args["progress"].as<vector<string>>(),
                                   args["fixed"].as<string>());
  
  // Run the real main function
  int result = real_main(argc - causal_argc - 1, &argv[causal_argc + 1], env);
  
  // Shut down the profiler
  profiler::get_instance().shutdown();
  
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

/** Intercept pthread functions and pass them on to the profiler **/
extern "C" {
  int pthread_create(pthread_t* thread,
                     const pthread_attr_t* attr,
                     thread_fn_t fn,
                     void* arg) {
    return profiler::get_instance().handle_pthread_create(thread, attr, fn, arg);                   
  }
  
  void pthread_exit(void* result) {
	  profiler::get_instance().handle_pthread_exit(result);
  }
  
  int pthread_mutex_lock(pthread_mutex_t* mutex) {
    profiler::get_instance().snapshot_delays();
    int result = real::pthread_mutex_lock()(mutex);            
    profiler::get_instance().skip_delays();
    
    return result;   
  }
  
  int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    profiler::get_instance().catch_up();
    return real::pthread_mutex_unlock()(mutex);
  }
  
  int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    profiler::get_instance().snapshot_delays();
    int result = real::pthread_cond_wait()(cond, mutex);            
    profiler::get_instance().skip_delays();
    
    return result;  
  }
  
  int pthread_cond_timedwait(pthread_cond_t* cond,
                             pthread_mutex_t* mutex,
                             const struct timespec* time) {
    FATAL << "Unsupported!";
    return -1;
  }
  
  int pthread_cond_signal(pthread_cond_t* cond) {
    profiler::get_instance().catch_up();
    return real::pthread_cond_signal()(cond);
  }
  
  int pthread_cond_broadcast(pthread_cond_t* cond) {
    profiler::get_instance().catch_up();
    return real::pthread_cond_broadcast()(cond);
  }
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
