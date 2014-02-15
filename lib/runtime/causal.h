#if !defined(CAUSAL_RUNTIME_CAUSAL_H)
#define CAUSAL_RUNTIME_CAUSAL_H

#include <cxxabi.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <fstream>
#include <list>
#include <new>
#include <sstream>

#include "perf.h"
#include "probe.h"
#include "util.h"

enum {
  SamplingSignal = 42,
  SamplingPeriod = 10000000
};

/// Possible execution modes
enum ProfilerMode {
  Clean,
  LegacyProfile,
  CausalProfile
};

class Causal {
private:
  /// Has the profiler been initialized?
  bool _initialized = false;
  
  /// The starting time for the program
  size_t _start_time;
  
  /// The current execution mode
  ProfilerMode _mode = Clean;
  
  /// A list of block addresses, if running in GetBlocks mode
  std::list<uintptr_t> _blocks;
  
  /// The address of the basic block selected for speedup or causal profiling
  uintptr_t _selected_block = 0;
  
  /// Record visits to the selected basic block
  size_t _block_visits = 0;
  
  /// Record samples executing in the selected basic block
  size_t _block_samples = 0;
  
  /// Record the total number of cycle samples
  size_t _total_samples = 0;
  
  /// Extra slowdown (in addition to instrumentation overhead) to add to selected block
  size_t _slowdown_size = 0;
  
  /// Number of nanoseconds to delay when causal profiling
  size_t _delay_size = 0;
  
  /// Count of all inserted delays when causal profiling is enabled
  static std::atomic<size_t> _delay_count;
  
  /// Count of delays inserted in the current thread
  static __thread size_t _local_delay_count;
  
  Causal() {}
  
  static void sampleSignal(int signum) {
    while(_local_delay_count < _delay_count) {
      _local_delay_count++;
      wait(instance()._delay_size);
    }
  }
  
  /**
  * Find the function containing a given address and return the symbol + offset in a string
  */
  string getMangledBlockName(uintptr_t address) {
    Dl_info info;

    // Return false if the dladdr() call fails
    if(dladdr((void*)address, &info) == 0)
      return "NA";

    // Return false if dladdr() didn't find a symbol and address
    if(info.dli_sname == NULL || info.dli_saddr == NULL)
      return "NA";
    
    std::stringstream ss;
    ss << info.dli_sname << "+" << (address - (uintptr_t)info.dli_saddr);

    return ss.str();
  }
  
  /**
  * Find the function containing a given address and return the symbol + offset in a string
  */
  string getBlockName(uintptr_t address) {
    Dl_info info;

    // Return false if the dladdr() call fails
    if(dladdr((void*)address, &info) == 0)
      return "NA";

    // Return false if dladdr() didn't find a symbol and address
    if(info.dli_sname == NULL || info.dli_saddr == NULL)
      return "NA";
    
    std::stringstream ss;
    
    // Attempt to demangle the function name
    char* demangled = abi::__cxa_demangle(info.dli_sname, NULL, NULL, NULL);
    // Add the best name to the string stream
    if(demangled == NULL) {
      ss << info.dli_sname;
    } else {
      ss << demangled;
      free(demangled);
    }
    // Add the block offset to the block name
    ss << "+" << (address - (uintptr_t)info.dli_saddr);
    
    return ss.str();
  }

public:
  static Causal& instance() {
    static char buf[sizeof(Causal)];
    static Causal* theInstance = new(buf) Causal();
    return *theInstance;
  }
  
  void setMode(ProfilerMode mode, uintptr_t block=0) {
    _mode = mode;
    _selected_block = block;
  }
  
  void setDelaySize(size_t delay) {
    _delay_size = delay;
  }
  
  void setSlowdownSize(size_t slowdown) {
    _slowdown_size = slowdown;
  }
  
  void addThread() {
    _local_delay_count = _delay_count;
    startSampling(SamplingPeriod, SamplingSignal);
  }
  
  void removeThread() {
    stopSampling();
  }
  
  void probe(uintptr_t ret) {
    if(!_initialized)
      return;
    
    if(_mode == Clean) {
      // Save the block address in the blocks list
      _blocks.push_back(ret);
      // Find the inserted probe (call instruction)
      Probe& p = Probe::get(ret);
      // Remove the probe
      p.remove();
      
    } else if(ret != _selected_block) {
      // This isn't the block we're looking for. Remove the probe and return
      Probe& p = Probe::get(ret);
      p.remove();
      return;
      
    } else {
      if(_slowdown_size > 0)
        wait(_slowdown_size);
      
      if(_mode == LegacyProfile) {
        // Record a visit to the selected block
        _block_visits++;
      
      } else if(_mode == CausalProfile) {
        // Make all other threads delay
        _local_delay_count++;
        _delay_count++;
      }
    }
    
    if(_mode == Clean || ret != _selected_block) {
      Probe& p = Probe::get(ret);
      p.remove();
    }
    
    if(_mode == LegacyProfile && ret == _selected_block) {
      _block_visits++;
    } else if(_mode == CausalProfile && ret == _selected_block) {
      _local_delay_count++;
      _delay_count++;
    }
  }
  
  void initialize() {
    signal(SamplingSignal, Causal::sampleSignal);
    _initialized = true;
    _start_time = getTime();
  }
  
  void shutdown() {
    static bool finished = false;
  
    if(!finished) {
      finished = true;
      size_t runtime = getTime() - _start_time;
  
      std::fstream versions("versions.profile", std::ofstream::out | std::ofstream::app);
      
      if(_mode ==  Clean) {
        // Mode = clean, total execution time
        versions << "clean" << "\t"
          << "runtime=" << runtime << "\n";

        // Write a list of all visited basic blocks to a file
        std::fstream blocks("blocks.profile", std::ofstream::out | std::ofstream::trunc);
        
        for(uintptr_t b : _blocks) {
          string block_name = getMangledBlockName(b);
          if(block_name != "NA")
            blocks << block_name << "\n";
        }
        
        blocks.close();
        
      } else if(_mode == LegacyProfile) {
        // Mode = legacy, total execution time, selected block, slowdown size, visits to selected block, samples in selected block, total samples
        versions << "legacy" << "\t"
          << "runtime=" << runtime << "\t"
          << "block=" << getBlockName(_selected_block) << "\t"
          << "slowdown=" << _slowdown_size << "\t"
          << "visits=" << _block_visits << "\t"
          << "samples=" << _block_samples << "\t"
          << "total_samples=" << _total_samples << "\n";
        
      } else if(_mode == CausalProfile) {
        size_t adjusted_runtime = runtime - _delay_size * _delay_count;
        // Mode = causal, total execution time, adjusted execution time, selected block, slowdown size, delay size
        versions << "causal" << "\t"
          << "runtime=" << runtime << "\t"
          << "adjusted_runtime=" << adjusted_runtime << "\t"
          << "block=" << getBlockName(_selected_block) << "\t"
          << "slowdown=" << _slowdown_size << "\t"
          << "delay=" << _delay_size << "\n";
      }
      
      versions.close();
    }
  }
};

#endif
