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
	BaselineResult _baseline;
	map<uintptr_t, SlowdownResult> _slowdown_results;
	multimap<uintptr_t, SpeedupResult> _speedup_results;
	
	Causal() {}
	
	void profilerThread() {
		while(true) {
			Host::wait(100 * Time::ms);
			DEBUG("Here");
			ProfileResult profile = CausalEngine::collectProfile();
			_baseline += CausalEngine::runBaseline(50 * Time::ms);
			for(uintptr_t b : profile.getUniqueBlocks()) {
				_slowdown_results[b] += CausalEngine::runSlowdown(b, 50 * Time::ms, 500 * Time::us);
				_speedup_results.emplace(b, CausalEngine::runSpeedup(b, 50 * Time::ms, (rand() % 10000 + 500) * Time::us));
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
			
			fprintf(stderr, "Slowdown results:\n");
			for(auto& r : _slowdown_results) {
				uintptr_t block = r.first;
				SlowdownResult& result = r.second;
				fprintf(stderr, "  %s\n    %f\n", Probe::get(block).getName().c_str(), result.marginalImpact(_baseline));
			}
			
			fprintf(stderr, "\nSpeedup results:\n");
			for(auto& r : _speedup_results) {
				uintptr_t block = r.first;
				SpeedupResult& result = r.second;
				fprintf(stderr, "%s,%f,%f\n", Probe::get(block).getName().c_str(), result.averageDelay(), result.speedup(_baseline));
			}
		}
	}
};

#endif
