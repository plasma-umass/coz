#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#include <map>
#include <string>

using namespace std;

/// The real main function, renamed by the LLVM pass
extern "C" int __real_main(int argc, char** argv);

void shutdown();
size_t getTime();
bool writeBlockName(FILE* fd, uintptr_t address);
uintptr_t getAddress(const char* function, int offset);
extern "C" void __causal_probe();

/// Only track basic block executions after ctors have run
bool initialized = false;

/// Record basic block visits
map<uintptr_t, size_t> block_visits;

enum { DelaySize = 1000 * 200 };

/// Possible execution modes
enum ProfilerMode {
  BlockProfile,
  TimeProfile,
  Speedup,
  CausalProfile
};

/// The current execution mode
ProfilerMode mode = BlockProfile;

/// The return address from the selected block's probe call
uintptr_t selected_block = 0;

/// The starting time for the main function
size_t start_time;

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
  if(argc >= 2 && strcmp(argv[1], "block_profile") == 0) {
    mode = BlockProfile;
    argc--;
    argv[1] = argv[0];
    argv = &argv[1];
  } else if(argc >= 2 && strcmp(argv[1], "time_profile") == 0) {
    mode = TimeProfile;
    argc--;
    argv[1] = argv[0];
    argv = &argv[1];
  } else if(argc >= 4 && strcmp(argv[1], "speedup") == 0) {
    mode = Speedup;
    selected_block = getAddress(argv[2], atoi(argv[3]));
    argc -= 3;
    argv[3] = argv[0];
    argv = &argv[3];
  } else if(argc >= 4 && strcmp(argv[1], "causal") == 0) {
    mode = CausalProfile;
    selected_block = getAddress(argv[2], atoi(argv[3]));
    argc -= 3;
    argv[3] = argv[0];
    argv = &argv[3];
  }
  
  initialized = true;
  start_time = getTime();
	int result = __real_main(argc, argv);
  shutdown();
	exit(result);
}

/**
 * Get the current time in nanoseconds
 */
size_t getTime() {
#if defined(__APPLE__)
  return mach_absolute_time();
#else
  struct timespec ts;
  if(clock_gettime(CLOCK_REALTIME, &ts)) {
    perror("getTime():");
    abort();
  }
  return ts.tv_nsec + ts.tv_sec * 1000 * 1000 * 1000;
#endif
}

/**
 * Find the function containing a given address and write the symbol and offset
 * to a file. Returns false if no symbol was found.
 */
bool writeBlockName(FILE* fd, uintptr_t address) {
  Dl_info info;
  
  // Return false if the dladdr() call fails
  if(dladdr((void*)address, &info) == 0)
    return false;
  
  // Return false if dladdr() didn't find a symbol and address
  if(info.dli_sname == NULL || info.dli_saddr == NULL)
    return false;
  
  fprintf(fd, "%s,%lu", info.dli_sname, address - (uintptr_t)info.dli_saddr);
  return true;
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
 * Write all profile output to the appropriate file. Mode information and the
 * total execution time are appended to the versions.profile file.
 */
void shutdown() {
  size_t runtime = getTime() - start_time;
  
  FILE* versions = fopen("versions.profile", "a");
  
  if(mode == BlockProfile) {
    FILE* blocks = fopen("block.profile", "w");
    for(const auto& e : block_visits) {
      if(writeBlockName(blocks, e.first))
        fprintf(blocks, ",%lu\n", e.second);
    }
    fclose(blocks);
    
    // Write the mode, selected block (none), and runtime to versions.profile
    fprintf(versions, "block_profile,NA,NA,%lu\n", runtime);
    
  } else if(mode == TimeProfile) {
    // TODO
  } else if(mode == Speedup) {
    // Write the mode, selected block, and runtime to versions.profile
    fprintf(versions, "speedup,");
    writeBlockName(versions, selected_block);
    fprintf(versions, ",%lu\n", runtime);
    
  } else if(mode == CausalProfile) {
    // TODO
  }
  
  fclose(versions);
}

/// Called from every instrumented basic block
extern "C" void __causal_probe() {
  if(!initialized)
    return;
  
  uintptr_t ret = (uintptr_t)__builtin_return_address(0);
  block_visits[ret]++;
  
  if(mode != Speedup || ret != selected_block) {
    size_t end_time = getTime() + DelaySize;
    while(getTime() < end_time) {
      __asm__("pause");
    }
  }
}

/// Intercept calls to exit() to ensure shutdown() is run first
extern "C" void exit(int status) {
	static auto real_exit = (__attribute__((noreturn)) void (*)(int))dlsym(RTLD_NEXT, "exit");
  shutdown();
	real_exit(status);
}

/// Intercept calls to _exit() to ensure shutdown() is run first	
extern "C" void _exit(int status) {
	static auto real_exit = (__attribute__((noreturn)) void (*)(int))dlsym(RTLD_NEXT, "_exit");
  shutdown();
	real_exit(status);
}

/// Intercept calls to _Exit() to ensure shutdown() is run first
extern "C" void _Exit(int status) {
	static auto real_exit = (__attribute__((noreturn)) void (*)(int))dlsym(RTLD_NEXT, "_Exit");
  shutdown();
	real_exit(status);
}
