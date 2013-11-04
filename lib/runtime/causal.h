#if !defined(CAUSAL_LIB_RUNTIME_CAUSAL_H)
#define CAUSAL_LIB_RUNTIME_CAUSAL_H

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
	Speedup
};

struct Causal : public SigThief<Host, SIGUSR1, SIGUSR2> {
private:
	atomic<bool> _initialized = ATOMIC_VAR_INIT(false);
	
	Mode _mode = Mode::Idle;
	uintptr_t _perturb_point;
	size_t _delay_size;
	
	atomic<size_t> _progress_visits;
	atomic<size_t> _perturb_visits;
	
	mutex _progress_points_mutex;
	vector<Probe*> _progress_points;
	
	mutex _blocks_mutex;
	vector<Probe*> _blocks;
	
	pthread_t profiler_thread;
	
	Causal() {}
	
	void slowdownExperiment(Probe* p, size_t delay, size_t duration, size_t min_trips = 10, size_t max_retries = 10) {	
		// Measure the baseline progress rate
		_progress_visits.store(0);
		Host::wait(duration);
		size_t control_count = _progress_visits.load();
		
		// Measure the perturbed progress rate
		_delay_size = delay;
		_perturb_point = p->getRet();
		p->restore();
		_progress_visits.store(0);
		_perturb_visits.store(0);
		Host::wait(duration);
		_perturb_point = 0;
		//p->remove();
		size_t treatment_count = _progress_visits.load();
		size_t num_perturbs = _perturb_visits.load();
		
		if(num_perturbs < min_trips || control_count < min_trips || treatment_count < min_trips) {
			if(num_perturbs > 0 && max_retries > 0) {
				slowdownExperiment(p, delay, duration * 2, min_trips, max_retries - 1);
			}
		} else {
			float control_progress_period = (float)duration / (float)control_count;
			float treatment_progress_period = (float)duration / (float)treatment_count;
			
			printf("%p: %f %f\n", (void*)p->getBase(), (float)delay, treatment_progress_period - control_progress_period);
		}
	}
	
	void profilerThread() {
		while(true) {
			// Sleep for 10ms
			Host::wait(500 * Host::Time::Millisecond);
			
			if(_blocks.size() > 0) {
				Probe* p = _blocks[rand() % _blocks.size()];
				slowdownExperiment(p, 1000 * Host::Time::Nanosecond, 100 * Host::Time::Millisecond);
			}
		}
	}
	
	static void* startProfilerThread(void*) {
		getInstance().profilerThread();
		return NULL;
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
