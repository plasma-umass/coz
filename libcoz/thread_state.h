#if !defined(CAUSAL_RUNTIME_THREAD_STATE_H)
#define CAUSAL_RUNTIME_THREAD_STATE_H

#include <atomic>

#include "ccutil/timer.h"

class thread_state {
public:
  bool in_use = false;      //< Set by the main thread to prevent signal handler from racing
  size_t local_delay = 0;   //< The count of delays (or selected line visits) in the thread
  perf_event sampler;       //< The sampler object for this thread
  timer process_timer;      //< The timer that triggers sample processing for this thread
  size_t pre_block_time;    //< The time saved before (possibly) blocking
  
  size_t arrivals = 0;      //< The number of task arrivals executed in this thread
  size_t saved_global_arrivals = 0; //< The number of arrivals from any thread from the start of the current sampling period
  size_t saved_local_arrivals = 0;  //< The number of arrivals from this thread from the start of the current sampling period
  
  inline void set_in_use(bool value) {
    in_use = value;
    std::atomic_signal_fence(std::memory_order_seq_cst);
  }
  
  bool check_in_use() {
    return in_use;
  }
};

#endif
