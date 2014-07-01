#include <unistd.h>

#include <iostream>
#include <string>

using std::cerr;
using std::cout;
using std::string;

#if !defined(CAUSAL_ROOT_PATH)
#error "The path to the causal root must be set in the CAUSAL_ROOT_PATH variable."
#endif

void show_usage(char* prog_name) {
  cerr << "Usage:\n"
    << "\t" << prog_name << " <program> args...\n"
    << "\t" << prog_name << " causal_args... --- <program> args...\n";
}

int main(int argc, char** argv, char** env) {
  if(argc < 2) {
    show_usage(argv[0]);
    return 2;
  }
  
  // Find the "---" separator between causal arguments and the program name
  size_t sep;
  for(sep = 1; sep < argc && argv[sep] != string("---"); sep++) {
    // Do nothing
  }
  
  cout << "Found separator at index " << sep << "\n";
  
  // If there is no "---", the program name must be in argv[1]
  char* prog_name = argv[1];
  
  // If the separator is the last argument no program was specified
  if(sep == argc - 1) {
    show_usage(argv[0]);
    return 2;
  }
  
  // If there is a valid separator, get the program name
  if(sep < argc - 1) {
    prog_name = argv[sep + 1];
  }
  
  // Set the preload string to the path to the debug causal library
  string causal_preload = CAUSAL_ROOT_PATH "/debug/lib/libcausal.so";
  
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
  
  // Execute the specified program. Pass all the arguments (including causal args) so they
  // can be handled by the preloaded library
  if(execvpe(prog_name, &argv[1], env)) {
    cerr << "exec failed!\n";
    return 2;
  }
}
