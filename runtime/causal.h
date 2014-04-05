#if !defined(CAUSAL_RUNTIME_CAUSAL_H)
#define CAUSAL_RUNTIME_CAUSAL_H

#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <ucontext.h>

#include <atomic>
#include <iterator>
#include <map>
#include <new>
#include <random>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

#include "basic_block.h"
#include "interval.h"
#include "perf.h"
#include "real.h"
#include "util.h"

enum {
  SamplingSignal = 42,
  SamplingPeriod = 1000000,
  SelectionSamples = 100,
  DelaySize = 1000
};

/// Possible execution modes
enum ProfilerMode {
  BlockProfile,
  TimeProfile,
  Speedup,
  CausalProfile
};

class Causal {
public:
  void initialize() {
    // Record the time at the beginning of execution
    _start_time = getTime();
    // Set up the sampling signal handler
    struct sigaction sa;
    sa.sa_sigaction = Causal::sampleSignal;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    //sigaddset(&sa.sa_mask, SamplingSignal);
    Real::sigaction()(SamplingSignal, &sa, nullptr);
    
    sa.sa_sigaction = Causal::onError;
    sa.sa_flags = SA_SIGINFO;
    Real::sigaction()(SIGSEGV, &sa, nullptr);
    
    _initialized.store(true);
  }
  
  void addFilePattern(std::string&& pat) {
    _file_patterns.insert(std::move(pat));
  }
  
  /**
   * Include functions from a file in the sampled set
   */
  bool includeFile(const std::string& path) {
    for(const std::string& pat : _file_patterns) {
      if(path.find(pat) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
  
  void addCounter(size_t* counter) {
    _counter = counter;
  }
  
  void addBlock(basic_block* block) {
    _blocks.insert(std::pair<interval, basic_block*>(block->getInterval(), block));
  }
  
  void shutdown() {
    if(_initialized.exchange(false) == true) {
      // shut down
      for(const auto& e : _blocks) {
        basic_block* b = e.second;
        if(b->observed()) {
          b->printInfo(SamplingPeriod, _total_samples);
        }
      }
    }
  }
  
  void addThread() {
    startSampling(SamplingPeriod, 42);
  }
  
  void removeThread() {
    shutdownPerf();
  }
  
  static Causal& getInstance() {
    static char buf[sizeof(Causal)];
    static Causal* theInstance = new(buf) Causal();
    return *theInstance;
  }
  
private:
  Causal() {}
  
  void sample(uintptr_t addr) {
    // Find the block corresponding to the sampled PC and record an observation
    auto sample_block_iter = _blocks.find(addr);
    if(sample_block_iter != _blocks.end()) {
      sample_block_iter->second->positiveSample();
    }
    // Increment the total number of PC samples
    _total_samples++;
    
    // If there is no selected block, pick one at random
    if(_selected_block == nullptr) {
      size_t i = _rng() % _blocks.size();
      auto iter = _blocks.begin();
      std::advance(iter, i);
      basic_block* b = iter->second;
      // Expected value for compare and swap
      basic_block* zero = nullptr;
      
      // Attempt to select the randomly chosen block
      if(_selected_block.compare_exchange_weak(zero, b)) {
        /*fprintf(stderr, "Selected block %s:%lu\n",
                b->getFunction()->getName().c_str(),
                b->getIndex());*/
        // Reset the sample counter
        _samples.store(0);
        // Get the trip count to activate trip counting
        getTripCount(b->getInterval().getBase());
      }
    }
    
    // Get the selected block pointer and check if it is set
    basic_block* b = _selected_block;
    if(b != nullptr) {
      // Get the trip count for the selected block
      long long visits = getTripCount(b->getInterval().getBase());
      // Record trips
      b->addVisits(visits);
      // Count PC samples that occur while this block is selected
      b->selectedSample();
      
      // Switch to a new block after SelectionSamples samples
      if(_samples++ == SelectionSamples) {
        _selected_block.store(nullptr);
      }
    }
  }
  
  static void sampleSignal(int signum, siginfo_t* info, void* p) {
    
    void* buf[3];
    int frames = backtrace(buf, 3);
    
    //ucontext_t* c = (ucontext_t*)p;
    //getInstance().sample(c->uc_mcontext.gregs[REG_RIP]);
    
    getInstance().sample((uintptr_t)buf[2]);
  }
  
  static void onError(int signum, siginfo_t* info, void* p) {
    fprintf(stderr, "Segfault at %p\n", info->si_addr);
    
    void* buf[256];
    int frames = backtrace(buf, 256);
    char** syms = backtrace_symbols(buf, frames);
    
    for(int i=0; i<frames; i++) {
      fprintf(stderr, "  %d: %s\n", i, syms[i]);
    }
    
    abort();
  }
  
  /// Pointer to the current counter (only one for now)
  size_t* _counter = nullptr;
  /// Set of patterns for libraries/executables that should be profiled
  std::set<std::string> _file_patterns;
  /// Has the profiler been initialized?
  std::atomic<bool> _initialized = ATOMIC_VAR_INIT(false);
  /// The total number of PC samples
  std::atomic<size_t> _total_samples = ATOMIC_VAR_INIT(0);
  /// The address of the basic block selected for "speedup". If the selected block
  /// is 0, then the profiler is idle
  std::atomic<basic_block*> _selected_block = ATOMIC_VAR_INIT(nullptr);
  /// The number of PC samples collected with the current selected block
  std::atomic<size_t> _samples = ATOMIC_VAR_INIT(0);
  /// The starting time for the program
  size_t _start_time;
  /// The map of basic blocks
  std::map<interval, basic_block*> _blocks;
  /// Random number generator
  std::mt19937 _rng;
};

#endif
