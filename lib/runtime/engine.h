#if !defined(CAUSAL_LIB_RUNTIME_ENGINE_H)
#define CAUSAL_LIB_RUNTIME_ENGINE_H

#include <pthread.h>

#include <atomic>
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
	RelaxTime = 10 * Time::ms
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
class CausalEngine : public SigThief<Host, DelaySignal, TrapSignal> {
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
	
	ProfileResult collectProfile() {
		// Reset the profile index
		_profile_index.store(0);
		_mode.store(Profile);
		
		// Pause threads and install all block probes
		_blocks_mutex.lock();
		for(uintptr_t b : _blocks) {
			Probe::get(b).restore();
		}
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
		Baseline,
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
	
	// Variables for collecting a conventional profile
	uintptr_t _profile[ProfileSize];	//< The array of observed block probe addresses
	atomic<size_t> _profile_index;		//< The next open index in the profile array
	
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
		DEBUG("Saved!");
		c.ip<uintptr_t>()--;
	}
	
public:
	/// Set up signal handlers at startup
	void initialize() {
		Host::setSignalHandler(SIGTRAP, __causal_signal_entry);
		Host::setSignalHandler(DelaySignal, __causal_signal_entry);
	}
	
	/// Called by __causal_signal_entry. Pass control to the appropriate signal handler
	void onSignal(int signum, siginfo_t* info, void* p) {
		if(signum == TrapSignal) {
			onTrap(p);
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
			Probe::get(ret, target).remove();
			_blocks_mutex.lock();
			_blocks.insert(ret);
			_blocks_mutex.unlock();
		}
	}
	
	/// Called by the instrumented program before calling an external function
	void extern_enter(void* fn, uintptr_t ret, uintptr_t target) {
		Probe::get(ret, target).remove();
	}
	
	/// Called by the instrumented program after calling an external function
	void extern_exit(uintptr_t ret, uintptr_t target) {
		Probe::get(ret, target).remove();
	}
};

#endif
