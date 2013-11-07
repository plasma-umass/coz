#if !defined(CAUSAL_LIB_RUNTIME_ENGINE_H)
#define CAUSAL_LIB_RUNTIME_ENGINE_H

#include <pthread.h>

#include <atomic>
#include <mutex>
#include <random>
#include <set>

#include "host.h"
#include "probe.h"
#include "sigthief.h"

/// Entry point for signal delivery
extern void __causal_signal_entry(int signum, siginfo_t* info, void* p);

// Constant definitions
enum {
	DelaySignal = SIGUSR1,
	PauseSignal = SIGUSR2,
	ProfileSize = 1000,
	StabilizationTime = Host::Time::Millisecond
};

/// The result returned from a slowdown experiment
class SlowdownResult {

};

/// The result returned from a speedup experiment
class SpeedupResult {

};

/// The result returned from a whole-program profile
class ProfileResult {
private:
	uintptr_t* _profile;
	default_random_engine _generator;
	uniform_int_distribution<size_t> _rng;
	
public:
	ProfileResult(uintptr_t* profile) : _profile(profile), _rng(0, ProfileSize - 1) {}
	
	uintptr_t getRandomBlock() {
		return _profile[_rng(_generator)];
	}
	
	set<uintptr_t> getUniqueBlocks() {
		set<uintptr_t> unique;
		for(size_t i=0; i<ProfileSize; i++) {
			unique.insert(_profile[i]);
		}
		return unique;
	}
};

/// Implements the mechanics of each type of performance experiment, and manages instrumentation
class CausalEngine : public SigThief<Host, DelaySignal, PauseSignal> {
public:	
	/*SlowdownResult runSlowdown(Block& b, size_t slowdown, size_t duration) {
		
	}
	
	SpeedupResult runSpeedup(Block& b, size_t speedup, size_t duration) {
		
	}*/
	
	ProfileResult collectProfile() {
		// Reset the profile index
		_profile_index.store(0);
		_mode.store(Profile);
		
		// Pause threads and install all block probes
		_blocks_mutex.lock();
		pauseThreads();
		for(uintptr_t b : _blocks) {
			Probe::restoreProbe(b);
		}
		resumeThreads();
		_blocks_mutex.unlock();
		
		// Spin until the profile is complete
		while(_profile_index < ProfileSize) {
			__asm__("pause");
		}
		
		// Return to idle mode and allow probes to clear on their own
		_mode.store(Idle);
		
		return ProfileResult(_profile);
	}
	
private:
	/// An enum that indicates the current mode of the causal engine
	enum Mode {
		Idle,
		Slowdown,
		Speedup,
		Profile
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
	
	// Variables for pausing and resuming threads
	atomic<size_t> _thread_arrivals;	//< The number threads that have reached the pause signal handler
	mutex _thread_blocker;	//< The mutex used to block threads until resumeThreads() is called
	
	// Variables for collecting a conventional profile
	uintptr_t _profile[ProfileSize];	//< The array of observed block probe addresses
	atomic<size_t> _profile_index;		//< The next open index in the profile array
	
	/// Pause this thread until the thread blocker mutex is released
	void onPause() {
		_thread_arrivals++;
		_thread_blocker.lock();
		_thread_blocker.unlock();
	}
	
	void pauseThreads() {
		size_t count = 0;
		_thread_blocker.lock();
		_thread_arrivals = ATOMIC_VAR_INIT(0);
		for(pthread_t t : Host::getThreads()) {
			if(pthread_kill(t, PauseSignal) == 0) {
				count++;
			}
		}
		while(_thread_arrivals.load() < count) {
			__asm__("pause");
		}
	}
	
	void resumeThreads() {
		_thread_blocker.unlock();
		Host::wait(StabilizationTime);
	}
	
	/// Delay the current thread and increment the delay counter
	void onDelay() {
		_total_delay += Host::wait(_delay_size);
		_delay_count++;
	}
	
public:
	/// Set up signal handlers at startup
	void initialize() {
		Host::setSignalHandler(DelaySignal, __causal_signal_entry);
		Host::setSignalHandler(PauseSignal, __causal_signal_entry);
	}
	
	/// Called by __causal_signal_entry. Pass control to the appropriate signal handler
	void onSignal(int signum, siginfo_t* info, void* p) {
		if(signum == PauseSignal) {
			onPause();
		} else if(signum == DelaySignal) {
			onDelay();
		} else {
			DEBUG("Unexpected signal received!");
			abort();
		}
	}
	
	/// Called by the instrumented program at each progress point
	void progress(uintptr_t ret, uintptr_t target) {
		if(_mode == Idle) {
			// Remove probes from progress points when in idle mode
			if(Probe::removeProbe(ret, target)) {
				// If the probe was removed successfully, add this probe to the set of progress points
				_progress_points_mutex.lock();
				_progress_points.insert(ret);
				_progress_points_mutex.unlock();
			}
		
		} else {
			_progress_visits++;
		}
	}
	
	/// Called by the instrumented program at the top of each block
	void probe(uintptr_t ret, uintptr_t target) {
		if(_mode == Slowdown && ret == _chosen_block) {
			_block_visits++;
			_delay_count++;
			if(_delay_size > 0)
				_total_delay += Host::wait(_delay_size);
			
		} else if(_mode == Speedup && ret == _chosen_block) {
			_block_visits++;
			// Signal every other thread
			for(pthread_t t : Host::getThreads()) {
				if(t != pthread_self()) {
					pthread_kill(t, DelaySignal);
				}
			}
			
		} else if(_mode == Profile) {
			// Increment the profile index, and save the previously set value
			size_t index = _profile_index++;
			// Add the probe's return address to the profile, if the profile isn't finished
			if(index < ProfileSize)
				_profile[index] = ret;
			
		} else {
			// Remove probes when in idle mode, or when the probe does not match the chosen block
			if(Probe::removeProbe(ret, target)) {
				_blocks_mutex.lock();
				_blocks.insert(ret);
				_blocks_mutex.unlock();
			}
		}
	}
	
	/// Called by the instrumented program before calling an external function
	void extern_enter(void* fn, uintptr_t ret, uintptr_t target) {
		Probe::removeProbe(ret, target);
	}
	
	/// Called by the instrumented program after calling an external function
	void extern_exit(uintptr_t ret, uintptr_t target) {
		Probe::removeProbe(ret, target);
	}
};

#endif
