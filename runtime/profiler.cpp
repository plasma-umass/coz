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

/// Count of all samples collected
atomic<size_t> totalSamples = ATOMIC_VAR_INIT(0);

/**
 * Parse profiling-related command line arguments, remove them from argc and
 * argv, then initialize the profiler.
 * @param argc Argument count passed to the injected main function
 * @param argv Argument array passed to the injected main function
 */
void profilerInit(int& argc, char**& argv) {
  bool include_main_exe = true;
  
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
  
  // Collect basic blocks (in inspect.cpp)
  registerBasicBlocks();
  
  // Set the starting time
  startTime = getTime();
  
  // Set up signal handlers
  setSignalHandler(CycleSampleSignal, cycleSampleReady);
  setSignalHandler(TripSampleSignal, tripSampleReady);
  setSignalHandler(SIGSEGV, onError);
}

void profilerShutdown() {
  if(shutDown.test_and_set() == false) {
    // shut down
    for(const auto& e : blocks) {
      basic_block* b = e.second;
      if(b->observed()) {
        b->printInfo(CycleSamplePeriod, totalSamples);
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

void registerBasicBlock(basic_block* block) {
  blocks.insert(pair<interval, basic_block*>(block->getInterval(), block));
}

void registerCounter(int kind, size_t* counter, const char* file, int line) {
  fprintf(stderr, "Counter registered\n");
}

void processCycleSample(PerfSampler::Sample& s) {
  fprintf(stderr, "cycle at %p\n", (void*)s.getIP());
  /*auto sample_block_iter = _blocks.find(s.getIP());
  if(sample_block_iter != _blocks.end()) {
    basic_block* b = sample_block_iter->second;

    b->positiveSample();

    // If there is no selected block, pick one at random
    if(_selected_block == nullptr) {
      // Expected value for compare and swap
      basic_block* zero = nullptr;

      // Attempt to select the randomly chosen block
      if(_selected_block.compare_exchange_weak(zero, b)) {
        // Reset the sample counter
        _samples.store(0);
        // Activate trip counting in the selected block
        makeTripCountSampler(b->getInterval().getBase()).start();
      }
    }
  }

  _total_samples++;

  basic_block* b = _selected_block;
  if(b != nullptr) {
    // Add to the count of samples collected while the current block was selected
    b->selectedSample();

    // Switch to a new block after SelectionSamples samples
    if(_samples++ == SelectionSamples) {
      getTripCountSampler().stop();
      fprintf(stderr, "Estimate for %s:%lu: %f visits\n",
              b->getFunction()->getName().c_str(),
              b->getIndex(),
              (float)getTripCountSampler().count());
      _selected_block.store(nullptr);
    }
  }*/
}

void processTripSample(PerfSampler::Sample& s) {
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
  fprintf(stderr, "Segfault at %p\n", info->si_addr);

  void* buf[256];
  int frames = backtrace(buf, 256);
  char** syms = backtrace_symbols(buf, frames);

  for(int i=0; i<frames; i++) {
    fprintf(stderr, "  %d: %s\n", i, syms[i]);
  }

  abort();
}

