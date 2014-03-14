#if !defined(CAUSAL_RUNTIME_CAUSAL_H)
#define CAUSAL_RUNTIME_CAUSAL_H

#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <ucontext.h>

#include <atomic>
#include <map>
#include <new>
#include <set>
#include <string>
#include <utility>

#include "basic_block.h"
#include "interval.h"
#include "perf.h"
#include "util.h"

enum {
  SamplingSignal = 42,
  SamplingPeriod = 1000000,
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
    sigaction(SamplingSignal, &sa, nullptr);
    
    _initialized.store(true);
  }
  
  void addFilePattern(std::string&& pat) {
    _file_patterns.insert(std::move(pat));
  }
  
  bool includeFile(const std::string& path) {
    for(const std::string& pat : _file_patterns) {
      if(path.find(pat) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
  
  void addBlock(basic_block block) {
    _blocks.insert(std::pair<interval, basic_block>(block.getInterval(), block));
  }
  
  void shutdown() {
    if(_initialized.exchange(false) == true) {
      // shut down
    }
  }
  
  void addThread() {
    startSampling(SamplingPeriod, 42);
  }
  
  void removeThread() {
    stopSampling();
  }
  
  static Causal& getInstance() {
    static char buf[sizeof(Causal)];
    static Causal* theInstance = new(buf) Causal();
    return *theInstance;
  }
  
private:
  Causal() {}
  
  void sample(uintptr_t addr) {
    auto i = _blocks.find(addr);
    if(i != _blocks.end()) {
      const basic_block& b = i->second;
      fprintf(stderr, "sample in %s : %s block %lu\n", 
              b.getFunction()->getFileName().c_str(),
              b.getFunction()->getName().c_str(),
              b.getIndex());
    }
  }
  
  static void sampleSignal(int signum, siginfo_t* info, void* p) {
    ucontext_t* c = (ucontext_t*)p;
    getInstance().sample(c->uc_mcontext.gregs[REG_RIP]);
  }
  
  /// Set of patterns for libraries/executables that should be profiled
  std::set<std::string> _file_patterns;
  /// Has the profiler been initialized?
  std::atomic<bool> _initialized = ATOMIC_VAR_INIT(false);
  /// The starting time for the program
  size_t _start_time;
  /// The map of basic blocks
  std::map<interval, basic_block> _blocks;
};

#endif
