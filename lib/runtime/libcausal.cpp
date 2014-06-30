#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#include <set>
#include <string>

#include "args.h"
#include "counter.h"
#include "log.h"
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
extern "C" void __causal_register_counter(CounterType kind, size_t* counter, const char* name) {
  profiler::registerCounter(new SourceCounter(kind, counter, name));
}

/**
 * Pass the real __libc_start_main this main function, then run the real main
 * function. This allows Causal to shut down when the real main function returns.
 */
int wrapped_main(int argc, char** argv, char** other) {
  // The set of file patterns (substrings) to include in the profile
  set<string> file_patterns;
  // Should the main executable be included in the profile
  bool include_main_exe = true;
  // The file where profile results should be written
  string output_filename = "profile.log";
  // The source line names that should be treated as progress points
  set<string> source_progress_names;
  // If set, the only source line that should be "sped up" by the profiler
  string fixed_line_name;

  // Walk through the arguments array to find any causal-specific arguments that change
  // the profiler's scope
  args a(argc, argv);
  for(auto arg = a.begin(); !arg.done(); arg.next()) {
    if(arg.get() == "--causal-exclude-main") {
      arg.drop();
      include_main_exe = false;
    } else if(arg.get() == "--causal-profile") {
      arg.drop();
      file_patterns.insert(arg.take());
    } else if(arg.get() == "--causal-fixed-line") {
      arg.drop();
      fixed_line_name = arg.take();
    } else if(arg.get() == "--causal-progress") {
      arg.drop();
      source_progress_names.insert(arg.take());
    } else if(arg.get() == "--causal-output") {
      arg.drop();
      output_filename = arg.take();
    }
  }
  
  // Commit changes to the argv array and update the argument count
  argc = a.commit(argv);

  // If the main executable hasn't been excluded, include its full path as a pattern
  if(include_main_exe) {
    file_patterns.insert(argv[0]);
  }

  // Walk through all the loaded executable images
  for(const auto& file : causal_support::get_loaded_files()) {
    const string& filename = file.first;
    uintptr_t load_address = file.second;
    
    // Check if the loaded file matches any of the specified patterns
    for(const string& pat : file_patterns) {
      if(filename.find(pat) != string::npos) {
        // When a match is found, tell the profiler to include the file
        profiler::include_file(filename, load_address);
        break;
      }
    }
  }
  
  // Start the profiler
  profiler::startup(output_filename, source_progress_names, fixed_line_name);
  // Run the real main function
  int result = real_main(argc, argv, other);
  // Shut down the profiler
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
