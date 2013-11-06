#if !defined(CAUSAL_LIB_RUNTIME_CAUSAL_H)
#define CAUSAL_LIB_RUNTIME_CAUSAL_H

#include <cxxabi.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "host.h"
#include "probe.h"
#include "sigthief.h"

using namespace std;

enum class Mode {
	Idle,
	Slowdown,
	Speedup,
	
};

enum {
	DelaySignal = SIGUSR1,
	PauseSignal = SIGUSR2
};

struct Causal : public SigThief<Host, DelaySignal, PauseSignal> {
private:
	atomic<bool> _initialized = ATOMIC_VAR_INIT(false);
	
	atomic<Mode> _mode = ATOMIC_VAR_INIT(Mode::Idle);
	atomic<uintptr_t> _perturb_point;
	atomic<size_t> _delay_size;
	
	atomic<size_t> _progress_visits;
	atomic<size_t> _perturb_visits;
	
	mutex _progress_points_mutex;
	vector<Probe*> _progress_points;
	
	mutex _blocks_mutex;
	vector<Probe*> _blocks;
	
	pthread_t profiler_thread;
	mutex _thread_blocker;
	atomic<size_t> _thread_arrivals;
	
	Causal() {}
	
	void insertProbes(Probe* perturb) {
		pauseThreads();
		_perturb_point = perturb->getRet();
		perturb->restore();
		for(Probe* p : _progress_points) {
			p->restore();
		}
		resumeThreads();
	}
	
	void slowdownExperiment(Probe* perturb, size_t delay, size_t max_duration, size_t min_trips = 50) {	
		// Set up the mode and delay so probes are not removed, but no delay is added
		_delay_size.store(0);
		_mode.store(Mode::Slowdown);
		
		// Insert probes at the perturbed point, and all progress points
		insertProbes(perturb);
		
		// Measure the baseline progress rate
		size_t duration = 0;
		size_t wait_size = Host::Time::Millisecond;
		
		// Clear counters, and start to measure the baseline
		_progress_visits.store(0);
		_perturb_visits.store(0);
		
		// Repeat the measurement until we have enough samples, or the maximum time has elapsed
		do {
			Host::wait(wait_size);
			duration += wait_size;
			wait_size *= 2;
		} while((_progress_visits.load() < min_trips || _perturb_visits.load() < min_trips) 
						&& duration + wait_size < max_duration / 2);
		
		// Read the results. If we didn't get enough samples, abort
		size_t control_count = _progress_visits.load();
		if(control_count < min_trips || _perturb_visits.load() < min_trips) {
			//DEBUG("aborting experiment at %p (progress: %zu, perturb: %zu)", (void*)perturb->getBase(),
			//		control_count, _perturb_visits.load());
			return;
		}
		
		// Measure the perturbed progress rate
		_delay_size.store(delay);
		_progress_visits.store(0);
		_perturb_visits.store(0);
		Host::wait(duration);
		size_t treatment_count = _progress_visits.load();
		size_t num_perturbs = _perturb_visits.load();
		
		// Return to idle mode. Probes will be removed as they are encountered
		_mode.store(Mode::Idle);
		
		float control_progress_period = (float)duration / (float)control_count;
		float treatment_progress_period = (float)duration / (float)treatment_count;
		Dl_info info;
		char* name;
		if(dladdr((void*)perturb->getBase(), &info) == 0) {
			name = "unknown";
		} else {
			name = abi::__cxa_demangle(info.dli_sname, NULL, NULL, NULL);
		}
		DEBUG("%s+%zu: impact = %f (progress: %zu, perturb: %zu)", name, perturb->getBase() - (uintptr_t)info.dli_saddr, 
				(treatment_progress_period - control_progress_period) / delay,
				treatment_count, num_perturbs);
	}
	
	void profilerThread() {
		while(true) {
			// Sleep for 10ms
			//Host::wait(500 * Host::Time::Millisecond);

			if(_blocks.size() > 0) {
				Probe* p = _blocks[rand() % _blocks.size()];
				slowdownExperiment(p, Host::Time::Millisecond, 500 * Host::Time::Millisecond);
			}
		}
	}
	
	static void* startProfilerThread(void*) {
		getInstance().profilerThread();
		return NULL;
	}
	
	void pauseThreads() {
		size_t count = 0;
		_thread_blocker.lock();
		_thread_arrivals = ATOMIC_VAR_INIT(0);
		for(pthread_t thread : Host::getThreads()) {
			if(pthread_kill(thread, SIGUSR2) == 0) {
				count++;
			}
		}
		while(_thread_arrivals.load() < count) {
			__asm__("pause");
		}
	}
	
	void resumeThreads() {
		_thread_blocker.unlock();
	}
	
	void onPause() {
		_thread_arrivals++;
		_thread_blocker.lock();
		_thread_blocker.unlock();
	}
	
	void onDelay() {
		DEBUG("TODO!");
	}
	
	static void startSignalHandler(int signum, siginfo_t* info, void* p) {
		if(signum == PauseSignal) {
			getInstance().onPause();
		} else if(signum == DelaySignal) {
			getInstance().onDelay();
		} else {
			DEBUG("Unexpected signal received!");
			abort();
		}
	}

public:
	static Causal& getInstance() {
		static char buf[sizeof(Causal)];
		static Causal* instance = new(buf) Causal();
		return *instance;
	}
	
	void initialize() {
		if(!_initialized.exchange(true)) {
			DEBUG("Initializing");
			Host::setSignalHandler(SIGUSR1, startSignalHandler);
			Host::setSignalHandler(SIGUSR2, startSignalHandler);
			profiler_thread = Host::createThread(startProfilerThread);
		}
	}
	
	void shutdown() {
		if(_initialized.exchange(false)) {
			DEBUG("Shutting down");
		}
	}
	
	void progress(uintptr_t ret, uintptr_t target) {
		if(_mode == Mode::Idle) {
			_progress_points_mutex.lock();
			Probe* probe = Probe::get(ret, target);
			if(probe) {
				probe->remove();
				_progress_points.push_back(probe);
			}
			_progress_points_mutex.unlock();
		} else {
			_progress_visits++;
		}
	}
	
	void probe(uintptr_t ret, uintptr_t target) {
		if(_mode == Mode::Slowdown && ret == _perturb_point) {
			_perturb_visits++;
			Host::wait(_delay_size);
			
		} else if(_mode == Mode::Speedup && ret == _perturb_point) {
			DEBUG("TODO!");
			
		} else {
			_blocks_mutex.lock();
			Probe* probe = Probe::get(ret, target);
			if(probe) {
				probe->remove();
				_blocks.push_back(probe);
			}
			_blocks_mutex.unlock();
		}
	}
	
	void extern_enter(void* fn, uintptr_t ret, uintptr_t target) {
		Probe* probe = Probe::get(ret, target);
		if(probe) {
			probe->remove();
		} else {
			DEBUG("Failed to find call to __causal_extern_enter() at %p", (void*)ret);
		}
	}
	
	void extern_exit(uintptr_t ret, uintptr_t target) {
		Probe* probe = Probe::get(ret, target);
		if(probe) {
			probe->remove();
		} else {
			DEBUG("Failed to find call to __causal_extern_exit() at %p", (void*)ret);
		}
	}
};

#endif
