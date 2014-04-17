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
void tripSampleReady(int, siginfo_t*, void*);
void onError(int, siginfo_t*, void*);

/**
 * A set of file name substrings. Any linked file that contains one of these
 * patterns will be processed and included in the profiled subset of running
 * code.
 */
set<string> filePatterns;

/// A map from symbols to function info
map<string, function_info*> functions;

/// A map from memory ranges to basic blocks
map<interval, basic_block*> blocks;

/// The starting time for the main executable
size_t startTime;

/// The per-thread cycle sampler
thread_local PerfSampler cycleSampler = PerfSampler::cycles(CycleSamplePeriod, CycleSampleSignal);

/// The per-thread trip count sampler
thread_local PerfSampler tripSampler;

/// A thread-local random number generator
thread_local std::default_random_engine rng;

/// Atomic flag cleared when shutdown procedure is run
atomic_flag shutDown = ATOMIC_FLAG_INIT;

/// Count of all cycle samples collected
atomic<size_t> totalSamples = ATOMIC_VAR_INIT(0);

/// Global round number
atomic<size_t> profilingRound = ATOMIC_VAR_INIT(0);

/// The thread-local round number. When out of sync with round, the thread will update trip count sampling
thread_local size_t localProfilingRound = 0;

/// The basic block currently selected for causal profiling
atomic<basic_block*> selectedBlock = ATOMIC_VAR_INIT(nullptr);

/// The number of samples collected for the current selection
atomic<size_t> selectedSamples = ATOMIC_VAR_INIT(0);

/// The current thread's selected block
thread_local basic_block* localSelectedBlock = nullptr;

/// Is the profiler operating with a fixed block instead of sampling blocks?
bool useFixedBlock = false;

/// The fixed block
basic_block* fixedBlock = nullptr;

/// Trip count sampling foo
atomic<size_t> tripCountEstimate = ATOMIC_VAR_INIT(0);
thread_local size_t tripCountPeriod = 0;
thread_local uint64_t lastTripCountTime = 0;

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
      useFixedBlock = true;
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
  if(useFixedBlock) {
    auto fn_iter = functions.find(fixed_block_symbol);
    if(fn_iter == functions.end()) {
      FATAL << "Unable to locate requested fixed profiling block: symbol not found";
    }
    
    function_info* fn = fn_iter->second;
    uintptr_t fixed_block_address = fn->getInterval().getBase() + fixed_block_offset;
    auto b_iter = blocks.find(fixed_block_address);
    
    if(b_iter == blocks.end()) {
      FATAL << "Unable to locate requested fixed profiling block: block not found";
    }
    
    fixedBlock = b_iter->second;
  }
  
  // Set the starting time
  startTime = getTime();
  
  // Set up signal handlers
  setSignalHandler(CycleSampleSignal, cycleSampleReady, TripSampleSignal);
  setSignalHandler(TripSampleSignal, tripSampleReady, CycleSampleSignal);
  setSignalHandler(SIGSEGV, onError);
  setSignalHandler(SIGFPE, onError);
}

void profilerShutdown() {
  if(shutDown.test_and_set() == false) {
    if(useFixedBlock) {
      // Just print stats for the fixed block
      fixedBlock->printInfo(CycleSamplePeriod, totalSamples);
      
      fprintf(stderr, "Estimated trips to selected block: %lu\n", tripCountEstimate.load());
      
    } else {
      for(const auto& e : blocks) {
        basic_block* b = e.second;
        if(b->observed()) {
          b->printInfo(CycleSamplePeriod, totalSamples);
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
  totalSamples++;

  // Try to locate the basic block that contains the IP from this sample
  auto sample_block_iter = blocks.find(s.getIP());
  // If a block was found...
  if(sample_block_iter != blocks.end()) {
    // Get the block
    basic_block* b = sample_block_iter->second;
    // Record a cycle sample in the block
    b->positiveSample();
    
    // If there is no selected block, try to set it to this one
    if(selectedBlock == nullptr) {
      // Is the profiler running with a single fixed block?
      if(useFixedBlock) {
        // If so, swap in the fixed block
        b = fixedBlock;
      }

      // Expected value for compare and swap
      basic_block* zero = nullptr;

      // Attempt to set the selected block
      if(selectedBlock.compare_exchange_weak(zero, b)) {
        // Clear count of samples on the current selection
        selectedSamples.store(0);
        profilingRound++;
      }
    }
  }
  
  // Get the currently selected block
  basic_block* selected = selectedBlock.load();
  // If there is a selected block...
  if(selected != nullptr) {
    // Increment the number of samples collected while this block is selected
    selected->selectedSample();
    
    // Increment the count of samples with the current selection. If the limit is reached...
    if(selectedSamples++ == SelectionSamples) {
      // Clear the selected block
      selectedBlock.store(nullptr);
    }
    
    if(localProfilingRound != profilingRound) {
      localProfilingRound = profilingRound;
      tripSampler = PerfSampler::trips((void*)selected->getInterval().getBase(), 
                                       TripSamplePeriod, TripSampleSignal);
      tripSampler.start(1);
    }
  }
}

void processTripSample(PerfSampler::Sample& s) {
  uint64_t currentTime = s.getTime();
  if(lastTripCountTime != 0 && tripCountPeriod != 0) {
    tripCountEstimate += (currentTime - lastTripCountTime) / tripCountPeriod;
  }
  
  lastTripCountTime = currentTime;
  tripCountPeriod = tripSampler.timeRunning();
  
  //fprintf(stderr, "Period: %lu\n", tripCountPeriod);
  
  /*basic_block* b = _selected_block;
  if(b != nullptr) {
    b->addVisits(TripSamplePeriod);
  }*/
}

void cycleSampleReady(int signum, siginfo_t* info, void* p) {
  cycleSampler.processSamples(processCycleSample);
}

void tripSampleReady(int signum, siginfo_t* info, void* p) {
  tripSampler.processSamples(processTripSample);
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
