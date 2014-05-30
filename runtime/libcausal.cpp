#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#include <set>
#include <string>

#include "args.h"
#include "counter.h"
#include "inspect.h"
#include "log.h"
#include "profiler.h"
#include "real.h"
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
  set<string> filePatterns;
  // Should the main executable be included in the profile
  bool include_main_exe = true;

  // Walk through the arguments array to find any causal-specific arguments that change
  // the profiler's scope
  args a(argc, argv);
  for(auto arg = a.begin(); !arg.done(); arg.next()) {
    if(arg.get() == "--causal-exclude-main") {
      arg.drop();
      include_main_exe = false;
    } else if(arg.get() == "--causal-profile") {
      arg.drop();
      filePatterns.insert(arg.take());
    }
  }

  // If the main executable hasn't been excluded, include its full path as a pattern
  if(include_main_exe) {
    char* main_exe = realpath(argv[0], nullptr);
    filePatterns.insert(main_exe);
    free(main_exe);
  }

  // Collect basic blocks and functions (in inspect.cpp)
  inspectExecutables(filePatterns);

  // The default filename where profile output will be written
  string output_filename = "profile.log";
  // A single basic block to target for profiling
  basic_block* fixed_block = nullptr;
  // Basic blocks that should be instrumented with perf-based progress counters
  map<basic_block*, string> perf_counter_blocks;

  // Walk through the arguments array to find any causal-specific arguments that must be
  // processed post-inspection
  for(auto arg = a.begin(); !arg.done(); arg.next()) {
    if(arg.get() == "--causal-select-block") {
      arg.drop();
      string name = arg.take();
      basic_block* b = findBlock(name);
      if(b != nullptr) {
        fixed_block = b;
        INFO << "Profiling with fixed block " << b->getFunction()->getName() << ":" << b->getIndex();
      } else {
        WARNING << "Unable to locate block " << name << ". Reverting to default mode.";
      }
    
    } else if(arg.get() == "--causal-progress") {
      arg.drop();
      string name = arg.take();
      basic_block* b = findBlock(name);
      if(b != nullptr) {
        // Save the block now, then generate a breakpoint-based counter later
        perf_counter_blocks[b] = name;
      } else {
        WARNING << "Unable to locate block " << name;
      }
    
    } else if(arg.get() == "--causal-output") {
      arg.drop();
      output_filename = arg.take();
    }
  }

  // Save changes to the arguments array
  argc = a.commit(argv);
  
  profiler::startup(output_filename, perf_counter_blocks, fixed_block);
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
