#include "profiler.h"

#include <execinfo.h>
#include <poll.h>
#include <pthread.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "counter.h"
#include "log.h"
#include "perf.h"
#include "support.h"
#include "util.h"

using namespace causal_support;
using namespace std;

int rt_tgsigqueueinfo(pid_t tgid, pid_t tid, int sig, siginfo_t *uinfo) {
  return syscall(__NR_rt_tgsigqueueinfo, tgid, tid, sig, uinfo);
}

namespace profiler {
  void onError(int, siginfo_t*, void*);
  void onPause(int, siginfo_t*, void*);
  void* profilerMain(void* arg);
  void logBaselineStart();
  void logBaselineEnd();
  void logSpeedupStart(shared_ptr<line>);
  void logSpeedupEnd(size_t, size_t);
  void logStartup();
  void logShutdown();
  void startPerformanceMonitoring();
  
  /// The file handle to log profiler output
  FILE* outputFile;
  
  /// Flag is set when shutdown has been run
  atomic_flag shutdownRun = ATOMIC_FLAG_INIT;
  
  /// The fixed line for causal profiling, if any
  shared_ptr<line> fixed_line;
  
  // Progress counter tracking
  unordered_set<Counter*> counters; //< A hash set of counter object pointers
  spinlock countersLock;           //< A lock to guard the counters hash set
  
  // Thread tracking
  unordered_map<pid_t, pthread_t> threads;  //< A hash map from tid to pthread identifier
  unordered_set<pid_t> deadThreads;        //< A hash set of threads that have exited
  spinlock threadsLock;                    //< A lock to guard the threads map
  /// A version number to indicate when the set of threads has changed
  atomic<size_t> threadsVersion = ATOMIC_VAR_INIT(0);
  
  /// The profiler thread handle
  pthread_t profilerThread;
  
  /// A flag to signal the profiler thread to exit
  atomic<bool> profilerRunning = ATOMIC_VAR_INIT(true);
  
  /// The map from source to memory locations constructed by causal_support
  memory_map& get_memory_map() {
    static char buf[sizeof(memory_map)];
    static memory_map* the_map = new(buf) memory_map();
    return *the_map;
  }
  
  void include_file(const string& filename, uintptr_t load_address) {
    PREFER(get_memory_map().process_file(filename, load_address))
      << "Failed to locate debug version of " << filename;
  }
  
  /**
   * Set up the profiling environment and start the main profiler thread
   * argv, then initialize the profiler.
   */
  void startup(const string& output_filename,
               const vector<string>& source_progress_names,
               const string& fixed_line_name) {
    // Set up signal handlers
    setSignalHandler(PauseSignal, onPause);
    setSignalHandler(SIGSEGV, onError);
    
    // If a non-empty fixed line was provided, attempt to locate it
    if(fixed_line_name != "") {
      fixed_line = get_memory_map().find_line(fixed_line_name);
      PREFER(fixed_line) << "Fixed line \"" << fixed_line_name << "\" was not found.";
    }

    // Open the output file
    outputFile = fopen(output_filename.c_str(), "a");
    REQUIRE(outputFile != NULL)
      << "Failed to open profiler output file: " << output_filename;
    
    // Create the profiler thread
    REQUIRE(Real::pthread_create()(&profilerThread, NULL, profilerMain, NULL) == 0)
      << "Failed to create profiler thread";
  
    // Create breakpoint-based progress counters for all the lines specified via command-line
    for(const string& line_name : source_progress_names) {
      shared_ptr<line> l = get_memory_map().find_line(line_name);
      if(l) {
        WARNING << "Found line \"" << line_name << "\" but breakpoint placement hasn't been implemented for lines.";
        // TODO: Place breakpoint-based counter
        // Old code was:
        // registerCounter(new PerfCounter(ProgressCounter, b->getInterval().getBase(), name.c_str()));
      } else {
        WARNING << "Progress line \"" << line_name << "\" was not found.";
      }
    }
  }

  /**
   * Flush output and terminate the profiler
   */
  void shutdown() {
    if(shutdownRun.test_and_set() == false) {
      profilerRunning.store(false);
      pthread_join(profilerThread, nullptr);
      
      // TODO: write out sampling profile results
      fclose(outputFile);
    }
  }

  /**
   * Register a new progress counter with the profiler. This may be called
   * from any of the application's threads.
   */
  void registerCounter(Counter* c) {
    countersLock.lock();

    // Insert the new counter
    counters.insert(c);
  
    // Unlock
    countersLock.unlock();
  }
  
  void threadStartup() {
    threadsLock.lock();
    threads.insert(pair<pid_t, pthread_t>(gettid(), pthread_self()));
    threadsVersion++;
    threadsLock.unlock();
  }
  
  void threadShutdown() {
    threadsLock.lock();
    deadThreads.insert(gettid());
    threadsVersion++;
    threadsLock.unlock();
  }
  
  class ProfilerState {
  private:
    enum Mode {
      Baseline,
      Speedup,
      Flush,
      Legacy
    };
    
    Mode mode = Baseline;
    
    default_random_engine generator;
    uniform_int_distribution<size_t> delayDist;
    vector<struct pollfd> pollers;
    map<pid_t, PerfEvent> events;
    map<Counter*, size_t> counterSnapshot;
    size_t currentVersion = 0;
    size_t roundSamples = 0;
    
    /// The count of visits or delays for each thread
    unordered_map<pid_t, size_t> localVisits;
    
    /// The currently selected line
    shared_ptr<line> selectedLine;
    
    /// The perf event configuration
    struct perf_event_attr pe = {
      .type = PERF_TYPE_SOFTWARE,
      .config = PERF_COUNT_SW_TASK_CLOCK,
      .sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CALLCHAIN,
      .sample_period = SamplePeriod,
      .wakeup_events = SampleWakeupCount
    };
    
  public:
    ProfilerState() : generator(getTime()), delayDist(0, 8) {}
    
    void processSample(const PerfEvent::SampleRecord& sample) {
      shared_ptr<line> l = get_memory_map().find_line(sample.ip);
      if(l != nullptr) {
        roundSamples++;
        // l->record_sample();
        
        if(mode == Baseline) {
          // If the baseline round has run long enough and we're in a known line,
          // use this line for the next speedup round
          if(roundSamples >= MinRoundSamples) {
            selectedLine = l;
          }
        } else if(mode == Speedup) {
          // Check if the sample is in the selected line
          if(l == selectedLine) {
            localVisits[sample.tid]++;
          }
        }
      }
    }
    
    void doBaseline() {
      if(!profilerRunning) {
        return;
      }
      
      mode = Baseline;
      roundSamples = 0;
      takeCounterSnapshot();
      
      logBaselineStart();
      
      while(roundSamples < MinRoundSamples || !selectedLine) {
        if(!profilerRunning) {
          break;
        }
        collectSamples();
      }
      
      logBaselineEnd();
    }
    
    void doSpeedup() {
      if(!profilerRunning) {
        return;
      }
      
      // Set the mode to speedup
      mode = Speedup;
      // Clear the count of samples seen this round
      roundSamples = 0;
      // Generate a random delay size
      size_t delay_size = delayDist(generator) * SamplePeriod / 8;
      // Count the total number of delays/visits required for each thread
      size_t delay_count = 0;
      
      // Save the state of all active counters
      takeCounterSnapshot();
      
      // Log the beginning of the speedup period
      logSpeedupStart(selectedLine);
      
      // Run until enough samples have been processed
      while(roundSamples < MinRoundSamples || !countersChanged()) {
        // Check for termination
        if(!profilerRunning) {
          break;
        }
        
        // Clear the per-thread counts of visits to the selected line
        localVisits.clear();
        
        // Process samples from all threads
        collectSamples();
        
        // Find the maximum number of visits by any one thread to the selected line
        size_t max_visits = 0;
        // Loop over threads to check for a new max
        for(auto& local : localVisits) {
          if(local.second > max_visits) {
            max_visits = local.second;
          }
        }
        
        // The total number of visits/delays should increase by the max visit count
        delay_count += max_visits;
        
        // If there was at least one visit to the selected line, send out delays to each thread
        if(max_visits > 0) {
          // Lock the set of threads
          threadsLock.lock();
          
          size_t max_pause = 0;
          
          // Loop over threads to send delays
          for(auto& thread : threads) {
            // Get the thread's task ID
            pid_t tid = thread.first;
            
            // Compute the required pause time for this thread
            size_t pause_time = delay_size * (max_visits - localVisits[tid]);
            
            // Set up siginfo_t structure to pass with signal
            siginfo_t info = {
              .si_code = SI_QUEUE,
              .si_pid = getpid(),
              .si_uid = getuid(),
              .si_value = {
                .sival_ptr = (void*)pause_time
              }
            };
            
            // Send the thread the pause signal
            if(rt_tgsigqueueinfo(getpid(), tid, PauseSignal, &info) != -1) {
              // If signaling the thread succeeded, update the max pause time
              if(pause_time > max_pause) {
                max_pause = pause_time;
              }
            }
          }
          
          // Unlock the set of threads
          threadsLock.unlock();
          
          // Wait until the thread with the longest pause time has had time to delay
          wait(max_pause);
        }
      }
      
      // Log the end of the speedup round
      logSpeedupEnd(delay_count, delay_size);
      
      // Flush any remaining samples before returning to baseline mode
      mode = Flush;
      collectSamples();
    }
    
    void takeCounterSnapshot() {
      counterSnapshot.clear();
      
      countersLock.lock();
      for(Counter* c : counters) {
        counterSnapshot[c] = c->getCount();
      }
      countersLock.unlock();
    }
    
    bool countersChanged() {
      return true;
      // if there are no saved counters, don't wait forever!
      if(counterSnapshot.size() == 0) {
        return true;
      }
      
      for(auto counter : counterSnapshot) {
        if(counter.first->getCount() != counter.second) {
          return true;
        }
      }
      return false;
    }
    
    void run() {
      // Log profiler parameters and calibration information to the profiler output
      logStartup();
    
      while(profilerRunning) {
        doBaseline();
        doSpeedup();
      }
    
      logShutdown();
    }
    
    void collectSamples() {
      updatePollers();
    
      int rc = poll(pollers.data(), pollers.size(), 10);
      REQUIRE(rc != -1) << "Failed to poll event files";
    
      if(rc > 0) {
        for(pair<const pid_t, PerfEvent>& event : events) {
          event.second.process(this);
        }
      }
    }
    
    void updatePollers() {
      size_t newest_version = threadsVersion.load();
    
      if(currentVersion < newest_version) {
        threadsLock.lock();
    
        // Remove any dead threads
        for(pid_t tid : deadThreads) {
          // Remove the thread from the thread map
          auto threads_iter = threads.find(tid);
          if(threads_iter != threads.end()) {
            threads.erase(threads_iter);
          }
      
          // Remove the thread's event from the events map
          auto events_iter = events.find(tid);
          if(events_iter != events.end()) {
            events_iter->second.stop();
            events.erase(events_iter);
          }
        }
    
        // Resize the pollers vector
        pollers.resize(threads.size());
    
        // Fill the pollers vector with pollfd structs
        size_t i = 0;
        for(pair<const pid_t, pthread_t> thread : threads) {
          pid_t tid = thread.first;
          // If this thread doesn't have an event yet, create one
          auto events_iter = events.find(tid);
          if(events_iter == events.end()) {
            events.emplace(tid, PerfEvent(pe, tid));
          }
      
          events[tid].start();
      
          pollers[i] = {
            .fd = events[tid].getFileDescriptor(),
            .events = POLLIN
          };
      
          i++;
        }
      
        currentVersion = threadsVersion.load();
    
        threadsLock.unlock();
      }
    }
  };
  
  /**
   * The body of the main profiler thread
   */
  void* profilerMain(void* arg) {
    ProfilerState s;
    s.run();
    
    return NULL;
  }

  /**
   * Log the start of a profile run, along with instrumentation calibration info
   */
  void logStartup() {
    fprintf(outputFile, "startup\ttime=%lu\n", getTime());
    fprintf(outputFile, "info\tsample-period=%lu\n", (size_t)SamplePeriod);
    //fprintf(outputFile, "info\tsource-counter-overhead=%lu\n", SourceCounter::calibrate());
    fprintf(outputFile, "info\tperf-counter-overhead=%lu\n", PerfCounter::calibrate());
  
    // Drop all counters, so we don't use any calibration counters during the real execution
    //counters.clear();
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
  void logSpeedupStart(shared_ptr<line> selected) {
    // Write out time, selected line, and progress counter values
    fprintf(outputFile, "start-speedup\tline=%s:%lu\ttime=%lu\n",
        selected->get_file()->get_name().c_str(), selected->get_line(), getTime());
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

  void onPause(int signum, siginfo_t* info, void* p) {
    size_t pause_time = (size_t)info->si_value.sival_ptr;
    wait(pause_time);
  }

  void onError(int signum, siginfo_t* info, void* p) {
    fprintf(stderr, "Signal %d at %p\n", signum, info->si_addr);

    void* buf[256];
    int frames = backtrace(buf, 256);
    char** syms = backtrace_symbols(buf, frames);

    for(int i=0; i<frames; i++) {
      fprintf(stderr, "  %d: %s\n", i, syms[i]);
    }

    Real::_exit()(2);
  }
}
