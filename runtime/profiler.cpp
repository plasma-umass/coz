#include <execinfo.h>
#include <poll.h>
#include <pthread.h>

#include <atomic>
#include <chrono>
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

using namespace std;

namespace profiler {
  enum Mode {
    Idle,
    Baseline,
    Seeking,
    Speedup,
    Legacy
  };
  
  enum { PageSize = 0x1000 };
  enum { PerfMapSize = 65 * PageSize };
  enum { PerfBufferSize = PerfMapSize - PageSize };
  
  struct Sample {
  public:
    perf_event_header hdr;
    uint64_t ip;
    uint32_t pid;
    uint32_t tid;
    uint64_t time;
  };
  
  void logStartup();
  void logShutdown();
  void onError(int, siginfo_t*, void*);
  void onPause(int, siginfo_t*, void*);
  void* profilerMain(void*);
  void startPerformanceMonitoring();

  /// The file handle to log profiler output
  FILE* outputFile;
  
  /// Flag is set when shutdown has been run
  atomic_flag shutdownRun = ATOMIC_FLAG_INIT;
  
  /// Map from counter addresses to counter tracking object
  unordered_set<Counter*> counters;
  
  /// Lock that guards the counters set
  atomic_flag counter_lock = ATOMIC_FLAG_INIT;

  /// The source of random bits
  default_random_engine generator;
  
  /// The distribution used to generate random delay sizes
  uniform_int_distribution<size_t> delayDist(0, SamplePeriod);
  
  /// Mutex used to block the profiler thread at startup
  pthread_mutex_t profilerMutex = PTHREAD_MUTEX_INITIALIZER;
  
  /// The profiler thread handle
  pthread_t profilerThread;
  
  /// The total number of CPUs enabled
  size_t cpu_count;
  
  /// Array of polling structs for per-cpu perf_event files
  struct pollfd* perf_fds;
  
  /// Array of per-cpu perf_event mapped file pointers
  struct perf_event_mmap_page** perf_maps;
  
  /// Array of per-cpu perf_event sampling ring buffers
  RingBuffer<PerfBufferSize>* perf_data;
  
  EventSet events;
  
  /**
   * Parse profiling-related command line arguments, remove them from argc and
   * argv, then initialize the profiler.
   * @param argc Argument count passed to the injected main function
   * @param argv Argument array passed to the injected main function
   */
  void startup(int& argc, char**& argv) {
    // See the random number generator
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    generator = default_random_engine(seed);
  
    set<string> filePatterns;
    bool include_main_exe = true;
  
    // Walk through the arguments array to find any causal-specific arguments that change
    // the profiler's scope
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
    basic_block* fixed_block = nullptr;
    map<basic_block*, string> perf_counter_blocks;
  
    // Walk through the arguments array to find any causal-specific arguments that must be
    // processed post-inspection
    for(auto arg = a.begin(); !arg.done(); arg.next()) {
      if(arg.get() == "--causal-select-block") {
        arg.drop();
        string name = arg.take();
        basic_block* b = findBlock(name);
        if(b != nullptr) {
          fixed_block = b;
          INFO << "Profiling with fixed block " << b->getFunction()->getName() << ":" << b->getIndex();
        } else {
          WARNING << "Unable to locate block " << name << ". Reverting to default mode.";
        }
      
      } else if(arg.get() == "--causal-progress") {
        arg.drop();
        string name = arg.take();
        basic_block* b = findBlock(name);
        if(b != nullptr) {
          // Save the block now, then generate a breakpoint-based counter later
          perf_counter_blocks[b] = name;
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
  
    // Lock the profiler mutex to block the main profiler thread
    REQUIRE(pthread_mutex_lock(&profilerMutex) == 0) << "Failed to lock profiler mutex";
    
    // Create the profiler thread
    REQUIRE(pthread_create(&profilerThread, NULL, profilerMain, fixed_block) == 0) << "Failed to create profiler thread";
  
    // Create breakpoint-based progress counters for all the command-line specified blocks
    for(pair<basic_block*, string> e : perf_counter_blocks) {
      basic_block* b = e.first;
      string name = e.second;
      registerCounter(new PerfCounter(ProgressCounter, b->getInterval().getBase(), name.c_str()));
    }
  
    // Start up performance monitoring
    startPerformanceMonitoring();
  
    // Set up signal handlers
    setSignalHandler(PauseSignal, onPause);
    setSignalHandler(SIGSEGV, onError);
    
    REQUIRE(pthread_mutex_unlock(&profilerMutex) == 0) << "Failed to unlock profiler mutex";
  }

  /**
   * Flush output and terminate the profiler
   */
  void shutdown() {
    if(shutdownRun.test_and_set() == false) {
      logShutdown();
      // TODO: write out sampling profile results
      fclose(outputFile);
    }
  }

  /**
   * Register a new progress counter with the profiler. This may be called
   * from any of the application's threads.
   */
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
  
  class Handler {
  public:
    static void processSample(const PerfEvent::SampleRecord& sample) {
      INFO << "sample! pid=" << sample.pid << " tid=" << sample.tid;
    }
  };
  
  /**
   * The body of the main profiler thread
   */
  void* profilerMain(void* arg) {
    // Block on startup until the main thread has finished starting perf events
    REQUIRE(pthread_mutex_lock(&profilerMutex) == 0) << "Failed to lock profiler mutex";
    REQUIRE(pthread_mutex_unlock(&profilerMutex) == 0) << "Failed to unlock profiler mutex";
    
    // If running on a fixed block, it will be passed as the thread argument
    basic_block* fixed_block = (basic_block*)arg;
    
    // Log profiler parameters and calibration information to the profiler output
    logStartup();
    
    while(true) {
      events.wait();
      events.process<Handler>();
    }
  }
  
  /**
   *
   */
  void startPerformanceMonitoring() {
    cpu_set_t cs;
    CPU_ZERO(&cs);
    sched_getaffinity(0, sizeof(cs), &cs);

    // Get the total number of active CPUs
    cpu_count = CPU_COUNT(&cs);
  
    // Allocate a table of pollable file descriptor entries
    perf_fds = new struct pollfd[cpu_count];
  
    // Allocate an array of perf mmap page pointers
    perf_maps = new struct perf_event_mmap_page*[cpu_count];
  
    // Allocate an array of perf ring buffers
    perf_data = new RingBuffer<PerfBufferSize>[cpu_count];
  
    // Open a perf file on each CPU
    for(int cpu = 0, i = 0; i < cpu_count; cpu++) {
      if(CPU_ISSET(cpu, &cs)) {
        INFO << "Starting perf on CPU " << cpu;
      
        // Set up the perf event file
        struct perf_event_attr pe = {
          .type = PERF_TYPE_SOFTWARE,
          .config = PERF_COUNT_SW_TASK_CLOCK,
          .size = sizeof(struct perf_event_attr),
          .disabled = 1,
          .inherit = 1,
          .sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME,
          .sample_period = SamplePeriod,
          .wakeup_events = 10
        };
        
        events.add(PerfEvent(pe, getpid(), cpu, -1, 0));
      
        // Move to the next index
        i++;
      }
    }
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
    for(Counter* c : counters) {
      fprintf(outputFile, "counter\tname=%s\tkind=%s\timpl=%s\tvalue=%lu\n",
          c->getName().c_str(), c->getKindName(), c->getImplName(), c->getCount());
    }
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
  void logSpeedupStart(basic_block* selected) {
    // Write out time, selected block, and progress counter values
    fprintf(outputFile, "start-speedup\tblock=%s:%lu\ttime=%lu\n",
        selected->getFunction()->getName().c_str(), selected->getIndex(), getTime());
    logCounters();
  }

  /**
   * Log the end of a speedup profiling round
   */
  void logSpeedupEnd(size_t delay_count, size_t delay_size) {
    // Write out time, progress counter values, delay count, and total delay
    fprintf(outputFile, "end-speedup\tdelays=%lu\tdelay-size=%lu\ttime=%lu\n",
        delay_count, delay_size, getTime());
    logCounters();
  }

/*void processCycleSample(PerfSampler::Sample& s) {
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
}*/

  void onPause(int signum, siginfo_t* info, void* p) {
    // TODO: pause here
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
}
