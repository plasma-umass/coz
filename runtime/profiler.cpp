#include <execinfo.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

#include "args.h"
#include "basic_block.h"
#include "counter.h"
#include "inspect.h"
#include "interval.h"
#include "log.h"
#include "perf.h"
#include "profiler.h"
#include "util.h"

using std::atomic;
using std::atomic_flag;
using std::default_random_engine;
using std::pair;
using std::set;
using std::string;
using std::uniform_int_distribution;
using std::unordered_set;

enum ProfilerMode {
  Idle,
  Baseline,
  Seeking,
  Speedup,
  Legacy
};

void cycleSampleReady(int, siginfo_t*, void*);
void logShutdown();
void logStartup();
void onError(int, siginfo_t*, void*);

// General execution info
atomic_flag shutDown = ATOMIC_FLAG_INIT;  //< Atomic flag cleared when shutdown procedure is run
FILE* outputFile; //< The file handle to log profiler output

/// The current profiling mode
atomic<ProfilerMode> mode = ATOMIC_VAR_INIT(Idle);

// Profiler round counters
atomic<size_t> globalRound = ATOMIC_VAR_INIT(0);        //< Global round number
thread_local size_t localRound = 0;                     //< The thread-local round number

// Sample counters
atomic<size_t> totalSamples = ATOMIC_VAR_INIT(0);       //< Count of all cycle samples collected
atomic<size_t> roundSamples = ATOMIC_VAR_INIT(0);       //< Samples collected during the current round

// Causal profiling bits
atomic<size_t> delaySize = ATOMIC_VAR_INIT(0);    //< The current delay size (in nanoseconds)
atomic<basic_block*> selectedBlock = ATOMIC_VAR_INIT(nullptr); //< The currently selected block
atomic<size_t> globalDelays = ATOMIC_VAR_INIT(0); //< Global count of delays added in the current round
thread_local size_t localDelays = ATOMIC_VAR_INIT(0);  //< Delays added to the current thread in the current round.
bool useFixedBlock = false;   //< Should the profiler run with a statically-selected block?
basic_block* fixedBlock;      //< The fixed block to profile

// Progress counter tracking
unordered_set<Counter*> counters;             //< Map from counter addresses to counter tracking object
atomic_flag counter_lock = ATOMIC_FLAG_INIT;  //< Lock acquired on insertions to the counters set

/// The per-thread cycle sampler
struct perf_event_attr perfConfig = {
  .type = PERF_TYPE_SOFTWARE,
  .config = PERF_COUNT_SW_CPU_CLOCK,
  .sample_period = SamplePeriod
};
thread_local PerfSampler cycleSampler(perfConfig, SampleSignal);

// RNG
default_random_engine generator;  //< The source of random bits
uniform_int_distribution<size_t> delayDist(0, SamplePeriod); //< The distribution used to generate random delay sizes


/**
 * Parse profiling-related command line arguments, remove them from argc and
 * argv, then initialize the profiler.
 * @param argc Argument count passed to the injected main function
 * @param argv Argument array passed to the injected main function
 */
void profilerInit(int& argc, char**& argv) {
  set<string> filePatterns;
  bool include_main_exe = true;
  
  args a(argc, argv);
  for(auto arg = a.begin(); !arg.done(); arg.next()) {
    if(arg.get() == "--causal-exclude-main") {
      arg.drop();
      include_main_exe = false;
    } else if(arg.get() == "--causal-profile") {
      arg.drop();
      filePatterns.insert(arg.take());
    }
  }
  
  // If the main executable hasn't been excluded, include its full path as a pattern
  if(include_main_exe) {
    char* main_exe = realpath(argv[0], nullptr);
    filePatterns.insert(main_exe);
    free(main_exe);
  }
  
  // Collect basic blocks and functions (in inspect.cpp)
  inspectExecutables(filePatterns);
  
  string output_filename = "profile.log";
  
  for(auto arg = a.begin(); !arg.done(); arg.next()) {
    if(arg.get() == "--causal-select-block") {
      arg.drop();
      string name = arg.take();
      basic_block* b = findBlock(name);
      if(b != nullptr) {
        useFixedBlock = true;
        fixedBlock = b;
        INFO << "Profiling with fixed block " << b->getFunction()->getName() << ":" << b->getIndex();
      } else {
        WARNING << "Unable to locate block " << name << ". Reverting to default mode.";
      }
      
    } else if(arg.get() == "--causal-progress") {
      arg.drop();
      string name = arg.take();
      basic_block* b = findBlock(name);
      if(b != nullptr) {
        registerCounter(new PerfCounter(ProgressCounter, b->getInterval().getBase(), name.c_str()));
      } else {
        WARNING << "Unable to locate block " << name;
      }
      
    } else if(arg.get() == "--causal-output") {
      arg.drop();
      output_filename = arg.take();
    }
  }
  
  // Save changes to the arguments array
  argc = a.commit(argv);

  // Open the output file
  outputFile = fopen(output_filename.c_str(), "a");
  REQUIRE(outputFile != NULL) << "Failed to open profiler output file: " << output_filename;
  
  // Log profiler parameters and calibration information to the profiler output
  logStartup();
  
  // Set up signal handlers
  setSignalHandler(SampleSignal, cycleSampleReady);
  setSignalHandler(SIGSEGV, onError);
  setSignalHandler(SIGFPE, onError);
}

void profilerShutdown() {
  if(shutDown.test_and_set() == false) {
    logShutdown();
    // TODO: write out sampling profile results
    fclose(outputFile);
  }
}

void threadInit() {
  cycleSampler.start();
}

void threadShutdown() {
  cycleSampler.stop();
}

void registerCounter(Counter* c) {
  // Lock the counters set
  while(counter_lock.test_and_set()) {
    __asm__("pause");
  }
  
  // Insert the new counter
  counters.insert(c);
  
  // Unlock
  counter_lock.clear();
}

/**
 * Log the start of a profile run, along with instrumentation calibration info
 */
void logStartup() {
  fprintf(outputFile, "startup\ttime=%lu\n", getTime());
  fprintf(outputFile, "info\tsample-period=%lu\n", (size_t)SamplePeriod);
  fprintf(outputFile, "info\tsource-counter-overhead=%lu\n", SourceCounter::calibrate());
  fprintf(outputFile, "info\tperf-counter-overhead=%lu\n", PerfCounter::calibrate());
  
  // Drop all counters, so we don't use any calibration counters during the real execution
  counters.clear();
}

/**
 * Log profiler shutdown
 */
void logShutdown() {
  fprintf(outputFile, "shutdown\ttime=%lu\n", getTime());
}

/**
 * Log the values for all known counters
 */
void logCounters() {
  // Lock the counters set
  while(counter_lock.test_and_set()) {
    __asm__("pause");
  }
  
  for(Counter* c : counters) {
    fprintf(outputFile, "counter\tname=%s\tkind=%s\timpl=%s\tvalue=%lu\n",
        c->getName().c_str(), c->getKindName(), c->getImplName(), c->getCount());
  }
  
  counter_lock.clear();
}

/**
 * Log the beginning of a baseline profiling round
 */
void logBaselineStart() {
  // Write out time and progress counter values
  fprintf(outputFile, "start-baseline\ttime=%lu\n", getTime());
  logCounters();
}

/**
 * Log the end of a baseline profiling round
 */
void logBaselineEnd() {
  // Write out time and progress counter values
  fprintf(outputFile, "end-baseline\ttime=%lu\n", getTime());
  logCounters();
}

/**
 * Log the beginning of a speedup profiling round
 */
void logSpeedupStart() {
  // Write out time, selected block, and progress counter values
  basic_block* b = selectedBlock.load();
  fprintf(outputFile, "start-speedup\tblock=%s:%lu\ttime=%lu\n",
      b->getFunction()->getName().c_str(), b->getIndex(), getTime());
  logCounters();
}

/**
 * Log the end of a speedup profiling round
 */
void logSpeedupEnd() {
  // Write out time, progress counter values, delay count, and total delay
  fprintf(outputFile, "end-speedup\tdelays=%lu\tdelay-size=%lu\ttime=%lu\n",
      globalDelays.load(), delaySize.load(), getTime());
  logCounters();
}

void processCycleSample(PerfSampler::Sample& s) {
  // Count the total number of samples over the whole execution
  totalSamples++;
  
  // Count samples for the current round
  size_t roundSample = roundSamples++;

  // Try to locate the basic block that contains the IP from this sample
  basic_block* sampleBlock = findBlock(s.getIP());
  // If a block was found...
  if(sampleBlock != nullptr) {
    // Record a cycle sample in the block
    sampleBlock->sample();
  }

  if(mode == Idle) {
    // The profiler just started up and should move to baseline mode immediately
    ProfilerMode idle = Idle;
    if(mode.compare_exchange_weak(idle, Baseline)) {
      logBaselineStart();
    }
    
  } else if(mode == Baseline) {
    // The profiler is measuring progress rates without a perturbation
    // When the round is over, move to seeking mode to choose a block for speedup
    if(roundSample == MaxRoundSamples) {
      logBaselineEnd();
      mode.store(Seeking);
      roundSamples.store(0);
    }
    
  } else if(mode == Seeking) {
    // The profiler is looking for a block to select for "speedup"
    basic_block* zero = nullptr;
    basic_block* next_block = sampleBlock;
    
    // If the profiler is running on a fixed block, set it as the next block
    if(useFixedBlock) {
      next_block = fixedBlock;
    }
    
    // If the current sample is in a known block, attempt to set it as the selected block
    if(next_block != nullptr && selectedBlock.compare_exchange_weak(zero, next_block)) {
      globalRound++;
      delaySize.store(delayDist(generator));
      mode.store(Speedup);
      roundSamples.store(0);
      logSpeedupStart();
    }
    
  } else if(mode == Speedup) {
    // The profiler is measuring progress rates with a "speedup" enabled
    if(localRound != globalRound) {
      localRound = globalRound;
      localDelays = 0;
    }
    
    basic_block* selected = selectedBlock.load();
    // If this thread is currently running the selected block...
    if(selected != nullptr && selected->getInterval().contains(s.getIP())) {
      if(localDelays < globalDelays) {
        // If this thread is behind on delays, just increment the local delay count
        localDelays++;
      } else {
        // If this thread is caught up, increment both delay counts to make other threads wait
        globalDelays++;
        localDelays++;
      }
    }
    
    size_t old_global_delays = globalDelays.load();
    size_t old_local_delays = __atomic_exchange_n(&localDelays, old_global_delays, __ATOMIC_SEQ_CST);
    
    // Catch up on delays
    if(old_local_delays < old_global_delays) {
      size_t delay_count = old_global_delays - old_local_delays;
      wait(delaySize * delay_count);
    }
    
    if(roundSample == MaxRoundSamples) {
      logSpeedupEnd();
      mode.store(Baseline);
      roundSamples.store(0);
      selectedBlock.store(nullptr);
      logBaselineStart();
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
