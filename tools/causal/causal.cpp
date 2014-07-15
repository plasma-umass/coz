#include <unistd.h>

#include <iostream>
#include <string>

#include "options.h"

using std::cerr;
using std::cout;
using std::string;

#if !defined(CAUSAL_ROOT_PATH)
#error "The path to the causal root must be set in the CAUSAL_ROOT_PATH variable."
#endif

int main(int argc, char** argv, char** env) {
  // Find the "---" separator between causal arguments and the program name
  size_t causal_argc;
  for(causal_argc = 1; causal_argc < argc && argv[causal_argc] != string("---"); causal_argc++) {
    // Do nothing
  }
  
  // Parse arguments preemptively (in case the user wants a help message)
  auto args = causal::parse_args(causal_argc, argv);
  
  // Show usage information if the help argument was passed
  if(args.count("help")) {
    causal::show_usage();
    return 1;
  }
  
  // If the separator is missing or in the last argument, no program was specified
  if(causal_argc >= argc - 1) {
    causal::show_usage();
    return 2;
  }
  
  // The program name comes immediately after the separator
  char* prog_name = argv[causal_argc + 1];
  
  // Set the program name in the first argument, otherwise file name resolution won't work
  argv[0] = prog_name;
  
#if !defined(NDEBUG)
  // Set the preload string to the path to the debug causal library
  string causal_preload = CAUSAL_ROOT_PATH "/debug/lib/libcausal.so";
#else
  // Preload the release version of the causal library
  string causal_preload = CAUSAL_ROOT_PATH "/release/lib/libcausal.so";
#endif
  
  // Loop over the environment array, looking for an LD_PRELOAD entry
  bool ld_preload_found = false;
  size_t env_size;
  for(env_size = 0; env[env_size] != nullptr && !ld_preload_found; env_size++) {
    string e(env[env_size]);
    // Check if this is the LD_PRELOAD entry
    if(e.find("LD_PRELOAD=") == 0) {
      ld_preload_found = true;
      // Add libcausal to the LD_PRELOAD variable
      causal_preload = e + ":" + causal_preload;
      // Update the environment
      env[env_size] = const_cast<char*>(causal_preload.c_str());
    }
  }
  
  // If no LD_PRELOAD variable was found, create a new environment with space for an LD_PRELOAD entry
  if(!ld_preload_found) {
    // Need space for each existing entry, a null terminator, and the LD_PRELOAD entry
    char** new_env = new char*[env_size + 2];
    // Copy the old environment over
    for(size_t i = 0; i < env_size; i++) {
      new_env[i] = env[i];
    }
    
    // Set the LD_PRELOAD variable
    causal_preload = string("LD_PRELOAD=") + causal_preload;
    new_env[env_size] = const_cast<char*>(causal_preload.c_str());
    
    // Set the null terminator
    new_env[env_size + 1] = nullptr;
    
    // Swap in the new environment
    env = new_env;
  }
  
  // Execute the specified program.
  // Pass all the arguments, including the causal tool name and all profiler args.
  if(execvpe(prog_name, argv, env)) {
    cerr << "exec failed!\n";
    return 2;
  }
}
