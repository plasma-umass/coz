#include <execinfo.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>

#include "basic_block.h"
#include "inspect.h"
#include "interval.h"
#include "log.h"
#include "perf.h"
#include "profiler.h"
#include "util.h"

using std::atomic;
using std::atomic_flag;
using std::map;
using std::pair;
using std::set;
using std::string;

void cycleSampleReady(int, siginfo_t*, void*);
void onError(int, siginfo_t*, void*);

// Binary inspection results
set<string> filePatterns;               //< File name substrings to include in the profiling set
map<string, function_info*> functions;  //< A map from symbols to function info
map<interval, basic_block*> blocks;     //< A map from memory ranges to basic blocks

// General execution info
size_t startTime;                         //< The starting time for the main executable
atomic_flag shutDown = ATOMIC_FLAG_INIT;  //< Atomic flag cleared when shutdown procedure is run

// Sampling data
atomic<size_t> samples = ATOMIC_VAR_INIT(0);            //< Count of all cycle samples collected
atomic<size_t> globalRound = ATOMIC_VAR_INIT(0);        //< Global round number
thread_local size_t localRound = 0;                     //< The thread-local round number
atomic<size_t> roundSamples = ATOMIC_VAR_INIT(0);       //< Samples collected during the current round
atomic<basic_block*> victim = ATOMIC_VAR_INIT(nullptr); //< The currently selected block
bool fixedVictim = false;                               //< Should the profiler run with a statically-selected block?

// Causal profiling bits
atomic<size_t> globalDelays = ATOMIC_VAR_INIT(0); //< Global count of delays added in the current round
thread_local size_t localDelays = 0;              //< Delays added to the current thread in the current round

/// The per-thread cycle sampler
thread_local PerfSampler cycleSampler = PerfSampler::cycles(CycleSamplePeriod, CycleSampleSignal);

/**
 * Parse profiling-related command line arguments, remove them from argc and
 * argv, then initialize the profiler.
 * @param argc Argument count passed to the injected main function
 * @param argv Argument array passed to the injected main function
 */
void profilerInit(int& argc, char**& argv) {
  bool include_main_exe = true;
  string fixed_block_symbol;
  size_t fixed_block_offset;
  
  // Loop over all arguments
  for(int i = 0; i < argc; i++) {
    int args_to_remove = 0;
    
    string arg(argv[i]);
    if(arg == "--causal-profile") {
      // Add the next argument as a file pattern for the profiler
      filePatterns.insert(argv[i+1]);
      args_to_remove = 2;
    } else if(arg == "--causal-exclude-main") {
      // Don't include the main executable in the profile
      include_main_exe = false;
      args_to_remove = 1;
    } else if(arg == "--causal-select-block") {
      fixedVictim = true;
      fixed_block_symbol = argv[i+1];
      fixed_block_offset = atoi(argv[i+2]);
      args_to_remove = 3;
    }
    
    if(args_to_remove > 0) {
      // Shift later arguments back `to_remove` spaces in `argv`
      for(int j = i; j < argc - args_to_remove; j++) {
        argv[j] = argv[j + args_to_remove];
      }
      // Overwrite later arguments with null
      for(int j = argc - args_to_remove; j < argc; j++) {
        argv[j] = nullptr;
      }
      // Update argc
      argc -= args_to_remove;
      // Decrement i, since argv[i] now holds an unprocessed argument
      i--;
    }
  }
  
  // If the main executable hasn't been excluded, include its full path as a pattern
  if(include_main_exe) {
    char* main_exe = realpath(argv[0], nullptr);
    filePatterns.insert(main_exe);
    free(main_exe);
  }
  
  // Collect basic blocks and functions (in inspect.cpp)
  inspectExecutables();
  
  // Is the selected block fixed?
  if(fixedVictim) {
    function_info* fn = nullptr;
    
    auto fn_iter = functions.find(fixed_block_symbol);
    if(fn_iter != functions.end()) {
      fn = fn_iter->second;
    } else {
      // Check for matching demangled names
      for(const auto& fn_entry : functions) {
        if(fn_entry.second->getName() == fixed_block_symbol) {
          fn = fn_entry.second;
          break;
        }
      }
    }
      
    REQUIRE(fn != nullptr) << "Unable to locate requested fixed profiling block: symbol not found";
    
    uintptr_t fixed_block_address = fn->getInterval().getBase() + fixed_block_offset;
    auto b_iter = blocks.find(fixed_block_address);
    
    if(b_iter == blocks.end()) {
      FATAL << "Unable to locate requested fixed profiling block: block not found";
    }
    
    victim = b_iter->second;
  }
  
  // Set the starting time
  startTime = getTime();
  
  // Set up signal handlers
  setSignalHandler(CycleSampleSignal, cycleSampleReady);
  setSignalHandler(SIGSEGV, onError);
  setSignalHandler(SIGFPE, onError);
}

void profilerShutdown() {
  if(shutDown.test_and_set() == false) {
    if(fixedVictim) {
      // Just print stats for the fixed block
      victim.load()->printInfo(CycleSamplePeriod, samples);
      
      //uint64_t period = periodSum.load() / periodCount.load();
      uint64_t runtime = getTime() - startTime;
      
      fprintf(stderr, "Total running time: %lu\n", runtime);
      fprintf(stderr, "Adjusted running time: %lu\n", runtime - DelaySize * globalDelays);
      
    } else {
      for(const auto& e : blocks) {
        basic_block* b = e.second;
        if(b->observed()) {
          b->printInfo(CycleSamplePeriod, samples);
        }
      }
    }
  }
}

void threadInit() {
  cycleSampler.start();
}

void threadShutdown() {
  cycleSampler.stop();
}

bool shouldIncludeFile(const string& filename) {
  for(const string& pat : filePatterns) {
    if(filename.find(pat) != string::npos) {
      return true;
    }
  }
  return false;
}

void registerFunction(function_info* fn) {
  functions.insert(pair<string, function_info*>(fn->getRawSymbolName(), fn));
}

void registerBasicBlock(basic_block* block) {
  blocks.insert(pair<interval, basic_block*>(block->getInterval(), block));
}

void registerCounter(int kind, size_t* counter, const char* file, int line) {
  INFO << "Counter registered from " << file << ":" << line;
}

void processCycleSample(PerfSampler::Sample& s) {
  // Keep track of the total number of cycle samples
  samples++;

  // Try to locate the basic block that contains the IP from this sample
  auto sample_block_iter = blocks.find(s.getIP());
  // If a block was found...
  if(sample_block_iter != blocks.end()) {
    // Get the block
    basic_block* b = sample_block_iter->second;
    // Record a cycle sample in the block
    b->sample();
    
    // If there is no selected block, try to set it to this one
    if(victim == nullptr) {
      // Expected value for compare and swap
      basic_block* zero = nullptr;

      // Attempt to set the selected block
      if(victim.compare_exchange_weak(zero, b)) {
        // Clear count of samples on the current selection
        roundSamples.store(0);
        globalRound++;
      }
    }
  }
  
  // Get the currently selected block
  basic_block* selected = victim.load();
  // If there is a selected block...
  if(selected != nullptr) {
    // If this thread is behind the current round, set up the new round
    if(localRound != globalRound) {
      localRound = globalRound;
      localDelays = 0;
    }
    
    // If this thread is currently running the victim block...
    if(selected->getInterval().contains(s.getIP())) {
      if(localDelays < globalDelays) {
        // If this thread is behind on delays, just increment the local delay count
        localDelays++;
      } else {
        // If this thread is caught up, increment both delay counts to make other threads wait
        globalDelays++;
        localDelays++;
      }
    }
    
    // Catch up on delays
    if(localDelays < globalDelays) {
      size_t delayTime = DelaySize * (globalDelays - localDelays);
      wait(delayTime);
    }
    
    // Increment the count of samples with the current selection.
    if(roundSamples++ == RoundSamples && !fixedVictim) {
      // If the round is over, clear the victim
      victim.store(nullptr);
      globalRound++;
    }
  }
}

void cycleSampleReady(int signum, siginfo_t* info, void* p) {
  cycleSampler.processSamples(processCycleSample);
}

void onError(int signum, siginfo_t* info, void* p) {
  fprintf(stderr, "Signal %d at %p\n", signum, info->si_addr);

  void* buf[256];
  int frames = backtrace(buf, 256);
  char** syms = backtrace_symbols(buf, frames);

  for(int i=0; i<frames; i++) {
    fprintf(stderr, "  %d: %s\n", i, syms[i]);
  }

  abort();
}
