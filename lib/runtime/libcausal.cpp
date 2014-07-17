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
  
  // Start the profiler on the main thread (round and delays are zero)
  profiler::get_instance().thread_startup(0, 0);
  
  // Run the real main function
  int result = real_main(argc - causal_argc - 1, &argv[causal_argc + 1], env);
  
  // Stop the profiler on the main thread
  profiler::get_instance().thread_shutdown();
  
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
