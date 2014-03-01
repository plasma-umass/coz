#if !defined(CAUSAL_RUNTIME_CAUSAL_H)
#define CAUSAL_RUNTIME_CAUSAL_H

#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include <map>
#include <new>

#include "perf.h"
#include "util.h"

enum {
  SamplingPeriod = 10000000,
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
private:
  /// Has the profiler been initialized?
  bool _initialized = false;
  
  /// The starting time for the program
  size_t _start_time;
  
  /// The current execution mode
  ProfilerMode _mode = BlockProfile;
  
  /// The address of the basic block selected for speedup or causal profiling
  uintptr_t _selected_block = 0;
  
  /// Record basic block visits
  std::map<uintptr_t, size_t> _block_visits;
  
  /// Count of all inserted delays when causal profiling is enabled
  std::atomic<size_t> _delay_count = ATOMIC_VAR_INIT(0);
  
  Causal() {}
  
  static void sampleSignal(int signum) {
    /*static __thread unsigned int magic;
    static __thread size_t period;
    static __thread size_t n;
    
    if(magic != 0xDEADBEEF) {
      period = 1;
      n = 0;
      magic = 0xDEADBEEF;
    }*/
    
    while(instance().localDelayCount() < instance()._delay_count) {
      instance().localDelayCount()++;
      wait(DelaySize);
      /*n++;
      if(n == period) {
        n = 0;
        wait(DelaySize * period++);
      }*/
    }
  }

public:
  static Causal& instance() {
    static char buf[sizeof(Causal)];
    static Causal* theInstance = new(buf) Causal();
    return *theInstance;
  }
  
  std::atomic<size_t>& localDelayCount() {
    /// Delays added to the current thread
    static __thread std::atomic<size_t> count;
    return count;
  }
  
  void setMode(ProfilerMode mode, uintptr_t block=0) {
    _mode = mode;
    _selected_block = block;
  }
  
  void addThread() {
    //if(_mode == CausalProfile) {
      localDelayCount().store(_delay_count.load());
      startSampling(SamplingPeriod, 42);
    //}
  }
  
  void removeThread() {
    
  }
  
  void probe(uintptr_t ret) {
    if(!_initialized)
      return;
  
    _block_visits[ret]++;

    if(_mode != Speedup || ret != _selected_block) {
      wait(DelaySize);
    }
  
    if(_mode == CausalProfile) {
      if(ret == _selected_block) {
        _delay_count++;
        localDelayCount()++;
      }
    }
  }
  
  void initialize() {
    signal(42, Causal::sampleSignal);
    _initialized = true;
    _start_time = getTime();
    srand((unsigned int)getTime());
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
  
  void shutdown() {
    static bool finished = false;
  
    if(!finished) {
      finished = true;
      size_t runtime = getTime() - _start_time;
  
      FILE* versions = fopen("versions.profile", "a");
  
      if(_mode == BlockProfile) {
        FILE* blocks = fopen("block.profile", "w");
        for(const auto& e : _block_visits) {
          if(writeBlockName(blocks, e.first))
            fprintf(blocks, ",%lu\n", e.second);
        }
        fflush(blocks);
        fclose(blocks);
    
        // Write the mode, selected block (none), and runtime to versions.profile
        fprintf(versions, "block_profile,NA,NA,%lu\n", runtime);
    
      } else if(_mode == TimeProfile) {
        // TODO
      } else if(_mode == Speedup) {
        // Write the mode, selected block, and runtime to versions.profile
        fprintf(versions, "speedup,");
        writeBlockName(versions, _selected_block);
        fprintf(versions, ",%lu\n", runtime);
    
      } else if(_mode == CausalProfile) {
        fprintf(versions, "causal,");
        writeBlockName(versions, _selected_block);
        fprintf(versions, ",%lu\n", runtime - DelaySize * _delay_count);
      }
  
      fflush(versions);
      fclose(versions);
    }
  }
};

#endif
