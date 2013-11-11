#if !defined(CAUSAL_LIB_RUNTIME_ENGINE_H)
#define CAUSAL_LIB_RUNTIME_ENGINE_H

#include <execinfo.h>
#include <pthread.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <set>

#include "context.h"
#include "host.h"
#include "probe.h"
#include "sigthief.h"

/// Entry point for signal delivery
extern void __causal_signal_entry(int signum, siginfo_t* info, void* p);

// Constant definitions
enum {
	DelaySignal = SIGUSR1,
	TrapSignal = SIGTRAP,
	ProfileSize = 1000,
	TargetSeekTime = 100 * Time::ms
};

/// The result returned from a baseline measurement of the progress rate
class BaselineResult {
private:
	size_t _duration;
	size_t _progress_count;
	
public:
	BaselineResult() : BaselineResult(0, 0) {}
	
	BaselineResult(size_t duration, size_t progress_count) :
		_duration(duration), _progress_count(progress_count) {}
	
	void operator+=(BaselineResult r) {
		_duration += r._duration;
		_progress_count += r._progress_count;
	}
	
	float period() {
		return (float)_duration / _progress_count;
	}
};

/// The result returned from a slowdown experiment
class SlowdownResult {
private:
	size_t _duration;
	size_t _progress_count;
	size_t _total_delay;
	size_t _delay_count;
	
public:
	SlowdownResult() : SlowdownResult(0, 0, 0, 0) {}
	
	SlowdownResult(size_t duration, size_t total_delay, size_t progress_count, size_t delay_count) :
		_duration(duration), _progress_count(progress_count),
		_total_delay(total_delay), _delay_count(delay_count) {}
	
	void operator+=(SlowdownResult r) {
		_duration += r._duration;
		_progress_count += r._progress_count;
		_total_delay += r._total_delay;
		_delay_count += r._delay_count;
	}
	
	bool valid() {
		return _delay_count > 0;
	}
	
	float period() {
		return (float)_duration / _progress_count;
	}
	
	float averageDelay() {
		return (float)_total_delay / _delay_count;
	}
	
	float marginalImpact(BaselineResult& control) {
		return (period() - control.period()) / averageDelay();
	}
};

/// The result returned from a speedup experiment
class SpeedupResult {
private:
	size_t _duration;
	size_t _total_delay;
	size_t _progress_count;
	size_t _block_count;
	size_t _delay_count;
	
public:
	SpeedupResult(size_t duration, size_t total_delay, size_t progress_count, size_t block_count, size_t delay_count) : 
	  _duration(duration), _total_delay(total_delay), _progress_count(progress_count), _block_count(block_count), _delay_count(delay_count) {}
	
	float averageDelay() {
		return (float)_total_delay / _delay_count;
	}
	
	float virtualDuration() {
		return _duration - averageDelay() * _block_count;
	}
	
	float period() {
		return virtualDuration() / _progress_count;
	}
	
	float speedup(BaselineResult& control) {
		return control.period() - period();
	}
	
	float marginalImpact(BaselineResult& control) {
		return speedup(control) / averageDelay();
	}
};

/// Implements the mechanics of each type of performance experiment, and manages instrumentation
class CausalEngine : public SigThief<Host, DelaySignal, TrapSignal, SIGSEGV, SIGBUS> {
public:
	BaselineResult runBaseline(size_t duration) {
		// Set up the mode and counter
		_mode.store(Baseline);
		setUp();
		
		// Restore instrumentation at all progress points
		_progress_points_mutex.lock();
		for(uintptr_t p : _progress_points) {
			Probe::get(p).restore();
		}
		_progress_points_mutex.unlock();
		
		// Wait for the specified duration, then end the experiment
		size_t real_duration = Host::wait(duration);
		_mode.store(Idle);
		
		return BaselineResult(real_duration, _progress_visits);
	}
	
	SlowdownResult runSlowdown(uintptr_t block, size_t duration, size_t delay) {
		// Set up the mode, delay, and all counters
		_mode.store(Slowdown);
		setUp(block, delay);
		
		// Restore instrumentation for all progress points and the chosen block
		_progress_points_mutex.lock();
		Probe::get(block).restore();
		for(uintptr_t p : _progress_points) {
			Probe::get(p).restore();
		}
		_progress_points_mutex.unlock();
		
		// Wait for the specified duration, then end
		size_t real_duration = Host::wait(duration);
		_mode.store(Idle);
		
		return SlowdownResult(real_duration, _total_delay, _progress_visits, _block_visits);
	}
	
	SpeedupResult runSpeedup(uintptr_t block, size_t duration, size_t delay) {
		// Set up the mode, delay, and counters
		_mode.store(Speedup);
		setUp(block, delay);
		
		// Restore instrumentation for all progress points and the chosen block
		_progress_points_mutex.lock();
		Probe::get(block).restore();
		for(uintptr_t p : _progress_points) {
			Probe::get(p).restore();
		}
		_progress_points_mutex.unlock();
		
		// Wait for the specified duration, then end
		size_t real_duration = Host::wait(duration);
		_mode.store(Idle);
		
		return SpeedupResult(real_duration, _total_delay, _progress_visits, _block_visits, _delay_count);
	}
	
	uintptr_t getActiveBlock(size_t target_time) {
		// Use the estimated rate of block discovery to compute a trap probability
		double p_trap = _period_estimate / target_time;
		// Non-zero probability, please
		if(p_trap < 0.001) p_trap = 0.001;
		
		bernoulli_distribution dist(p_trap);
		
		// Set up for seeking mode
		unique_lock<mutex> l(_blocks_mutex);
		_mode.store(Seeking);
		_chosen_block.store(0);
		
		size_t start_time = Host::getTime();
		do {
			// Place traps on a random subset of blocks
			for(uintptr_t b : _blocks) {
				if(dist(_rng)) {
					Probe::get(b).restore();
				}
			}
			// Wait
			_seeking_cv.wait_for(l, chrono::nanoseconds(2*target_time));
		} while(_mode == Seeking);
		
		// Estimate the period for this run
		size_t elapsed = Host::getTime() - start_time;
		double new_period_estimate = elapsed * p_trap;
		
		// Take a small step toward the new estimate (moving average)
		_period_estimate = (4 * _period_estimate + new_period_estimate) / 5;
		
		return _chosen_block;
	}
	
private:
	/// An enum that indicates the current mode of the causal engine
	enum Mode {
		Idle,
		Baseline,
		Slowdown,
		Speedup,
		Seeking
	};
	
	atomic<Mode> _mode = ATOMIC_VAR_INIT(Mode::Idle);	//< The current mode
	
	// Variables for tracking progress points
	set<uintptr_t> _progress_points;	//< The set of all discovered progress points
	mutex _progress_points_mutex;			//< Mutex to protect the progress points set
	atomic<size_t> _progress_visits;	//< Counter of visits to progress points
	
	// Variables for tracking instrumented blocks
	set<uintptr_t> _blocks;						//< The set of all discovered block probes
	mutex _blocks_mutex;							//< Mutex to protect the blocks set
	atomic<uintptr_t> _chosen_block;	//< The currently selected block for a speedup or slowdown
	atomic<size_t> _block_visits;			//< Counter of visits to the chosen block
	
	// Variables for managing inserted delays
	atomic<size_t> _delay_size;		//< The size of a single delay
	atomic<size_t> _delay_count;	//< The number of delays added
	atomic<size_t> _total_delay;	//< The total delay time added
	
	// Active block seeking variables
	condition_variable _seeking_cv;			//< CV used to sleep while seeking (with _blocks_mutex)
	default_random_engine _rng;					//< Random generator engine for setting traps
	double _period_estimate = Time::s;	//< Estimate of the time between unique block executions (ns)
	
	void setUp(uintptr_t block = 0, size_t delay = 0) {
		_progress_visits.store(0);
		_chosen_block.store(block);
		_block_visits.store(0);
		_delay_count.store(0);
		_total_delay.store(0);
		_delay_size.store(delay);
	}
	
	/// Delay the current thread and increment the delay counter
	void onDelay() {
		_total_delay += Host::wait(_delay_size);
		_delay_count++;
	}
	
	void onTrap(Context c) {
		c.ip<uintptr_t>()--;
	}

	void onFault(void* addr, Context c) {
		Dl_info sym_info;
		dladdr(c.ip<void*>(), &sym_info);
		printf("Fault in function %s (%p), accessing %p\n", sym_info.dli_sname, c.ip<void*>(), addr);
		
		void* buf[100];
		size_t num = backtrace(buf, 100);
		char** strings = backtrace_symbols(buf, num);

		if(strings == NULL) {
			perror("backtrace_symbols");
			abort();
		}

		for(size_t i=0; i<num; i++) {
			fprintf(stderr, "\t%s\n", strings[i]);
		}

		fprintf(stderr, "Threads:\n");
		for(pthread_t t : Host::getThreads()) {
			fprintf(stderr, "  %p\n", (void*)t);
		}

		DEBUG("Died! Waiting for gdb attach. PID=%d", getpid());
		for(;;){}
	}
	
public:
	/// Set up signal handlers at startup
	void initialize() {
		Host::initialize();
		Host::setSignalHandler(SIGSEGV, __causal_signal_entry);
		Host::setSignalHandler(SIGBUS, __causal_signal_entry);
		Host::setSignalHandler(TrapSignal, __causal_signal_entry);
		Host::setSignalHandler(DelaySignal, __causal_signal_entry);
	}
	
	/// Called by __causal_signal_entry. Pass control to the appropriate signal handler
	void onSignal(int signum, siginfo_t* info, void* p) {
		if(signum == TrapSignal) {
			onTrap(p);
		} else if(signum == DelaySignal) {
			onDelay();
		} else if(signum == SIGSEGV || signum == SIGBUS) {
			onFault(info->si_addr, p);
		} else {
			DEBUG("Unexpected signal received!");
			abort();
		}
	}
	
	/// Called by the instrumented program at each progress point
	void progress(uintptr_t ret, uintptr_t target) {
		if(_mode == Idle) {
			// Remove probes from progress points when in idle mode
			Probe::get(ret, target).remove();
			// Add this probe to the set of progress points
			_progress_points_mutex.lock();
			_progress_points.insert(ret);
			_progress_points_mutex.unlock();
		
		} else {
			_progress_visits++;
		}
	}
	
	/// Called by the instrumented program at the top of each block
	void probe(uintptr_t ret, uintptr_t target) {
		if(_mode == Slowdown && ret == _chosen_block) {
			_block_visits++;
			//_delay_count++; <- skip this, since it will always be the same as _block_visits
			if(_delay_size > 0)
				_total_delay += Host::wait(_delay_size);
			
		} else if(_mode == Speedup && ret == _chosen_block) {
			_block_visits++;
			// Signal every other thread
			Host::lockThreads();
			for(pthread_t t : Host::getThreads()) {
				if(t != pthread_self()) {
					pthread_kill(t, DelaySignal);
				}
			}
			Host::unlockThreads();
			
		} else if(_mode == Seeking) {
			// Lock the set of blocks
			unique_lock<mutex> l(_blocks_mutex);
			// Record the current block
			_chosen_block.store(ret);
			// End seeking mode
			_mode.store(Idle);
			// Inform the profiler thread that a block has been selected
			_seeking_cv.notify_one();
			
		} else {
			// Remove probes when in idle mode, or when the probe does not match the chosen block
			Probe::get(ret, target).remove();
			_blocks_mutex.lock();
			_blocks.insert(ret);
			_blocks_mutex.unlock();
		}
	}
	
	/// Called by the instrumented program before calling an external function
	void extern_call(void* fn, uintptr_t ret, uintptr_t target) {
		Probe::get(ret, target).remove();
	}
};

#endif
