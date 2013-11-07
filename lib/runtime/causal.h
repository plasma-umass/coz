#if !defined(CAUSAL_LIB_RUNTIME_CAUSAL_H)
#define CAUSAL_LIB_RUNTIME_CAUSAL_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "engine.h"

using namespace std;

struct Causal : CausalEngine {
private:
	atomic<bool> _initialized = ATOMIC_VAR_INIT(false);
	
	Causal() {}
	
	/*void slowdownExperiment(Probe* p, size_t delay, size_t duration) {	
		// Set up the mode and delay so probes are not removed, but no delay is added
		_delay_size.store(0);
		_mode.store(Mode::Slowdown);
		
		// Insert probes and wait for threads to resume
		insertProbes(p);
		Host::wait(Host::Time::Millisecond);
		
		// Clear the counter and run the control experiment
		_progress_visits.store(0);
		size_t real_duration = Host::wait(duration);
		p->addResults(real_duration, _progress_visits);
		
		// Clear counters again and run the perturbed experiment
		_delay_size.store(delay);
		_progress_visits.store(0);
		_perturb_visits.store(0);
		_perturb_time.store(0);
		real_duration = Host::wait(duration);
		p->addResults(real_duration, _progress_visits, _perturb_time, _perturb_visits);
		
		// Return to idle mode (probes will be removed as encountered)
		_mode.store(Mode::Idle);
	}
	
	void measureBlock(Probe* p, size_t delay, size_t duration) {	
		// Set up the mode and delay so probes are not removed, but no delay is added
		_delay_size.store(0);
		_mode.store(Mode::Slowdown);
		
		// Insert probes and wait for threads to resume
		insertProbes(p);
		Host::wait(Host::Time::Millisecond);
		
		// Clear the counter and run the control experiment
		_progress_visits.store(0);
		size_t real_duration = Host::wait(duration);
		p->addResults(real_duration, _progress_visits);
		
		// Clear counters again and run the perturbed experiment
		_delay_size.store(delay);
		_progress_visits.store(0);
		_perturb_visits.store(0);
		_perturb_time.store(0);
		real_duration = Host::wait(duration);
		p->addResults(real_duration, _progress_visits, _perturb_time, _perturb_visits);
		
		// Clear counters and run a speedup experiment
		_progress_visits.store(0);
		_perturb_visits.store(0);
		_perturb_time.store(0);
		_mode.store(Mode::Speedup);
		
		real_duration = Host::wait(duration);
		DEBUG("%zu progress visits, slowdown of %ld in %zu visits", _progress_visits.load(), -_perturb_time.load(), _perturb_visits.load());
		p->addResults(real_duration, _progress_visits, -_perturb_time, _perturb_visits);
		
		_mode.store(Mode::Idle);
	}*/
	
	void profilerThread() {
		while(true) {
			// Sleep for 10ms
			Host::wait(100 * Host::Time::Millisecond);

			/*if(_blocks.size() > 0) {
				uintptr_t p = _blocks[rand() % _blocks.size()];
				measureBlock(Probe::get(p), 500 * Host::Time::Microsecond, 50 * Host::Time::Millisecond);
				//slowdownExperiment(Probe::get(p), 500 * Host::Time::Microsecond, 50 * Host::Time::Millisecond);
			}*/
			ProfileResult r = CausalEngine::collectProfile();
			set<uintptr_t> unique = r.getUniqueBlocks();
			DEBUG("%ld unique blocks:", unique.size());
			for(uintptr_t b : unique) {
				DEBUG("  %p", (void*)b);
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
			CausalEngine::initialize();
			Host::createThread(startProfilerThread);
		}
	}
	
	void shutdown() {
		if(_initialized.exchange(false)) {
			DEBUG("Shutting down");
			
			/*for(uintptr_t p : _blocks) {
				Probe::get(p)->showResults();
			}*/
		}
	}
};

#endif
