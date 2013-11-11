#if !defined(CAUSAL_LIB_RUNTIME_CAUSAL_H)
#define CAUSAL_LIB_RUNTIME_CAUSAL_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "engine.h"

using namespace std;

struct DebugInfo {
	uintptr_t block;
	const char* filename;
	uint32_t start;
	uint32_t end;
};	

struct Causal : CausalEngine {
private:
	atomic<bool> _initialized = ATOMIC_VAR_INIT(false);
	BaselineResult _baseline;
	map<uintptr_t, SlowdownResult> _slowdown_results;
	multimap<uintptr_t, SpeedupResult> _speedup_results;
	multimap<uintptr_t, DebugInfo*> _debug_info;
	
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
	
	set<const DebugInfo*> findDebugInfo(uintptr_t ret) {
		// Find the nearest block staring address less than ret
		uintptr_t block = 0;
		for(const auto& i : _debug_info) {
			if(i.first > ret) break;
			else block = i.first;
		}
		
		// Return if nothing was found
		if(block == 0) return set<const DebugInfo*>();
		
		// Get the range of equal elements. There may be multiple DebugInfos for this block
		auto range = _debug_info.equal_range(block);
		// Add all DebugInfos to the result set
		set<const DebugInfo*> result;
		for(auto iter = range.first; iter != range.second; iter++) {
			result.insert(iter->second);
		}
		
		return result;
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

			pthread_t profiler_thread;
			if(Host::real_pthread_create(&profiler_thread, NULL, startProfilerThread, NULL)) {
				perror("Failed to start profiler thread:");
				abort();
			}
			DEBUG("Profiler thread is %p", profiler_thread);
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
				for(const DebugInfo* info : findDebugInfo(block)) {
					if(info->start != info->end)
						fprintf(stderr, "    %s : %d-%d\n", info->filename, info->start, info->end);
					else
						fprintf(stderr, "    %s : %d\n", info->filename, info->start);
				}
			}
			
			/*fprintf(stderr, "\nSpeedup results:\n");
			for(auto& r : _speedup_results) {
				uintptr_t block = r.first;
				SpeedupResult& result = r.second;
				fprintf(stderr, "%s,%f,%f\n", Probe::get(block).getName().c_str(), result.averageDelay(), result.speedup(_baseline));
			}*/
		}
	}

	int fork() {
		int result = Host::real_fork();
		if(result == 0) {
			// TODO: Clear profiling data (it will stay with the parent process)
			_initialized.store(false);
			initialize();
		}
		return result;
	}
	
	void debug_info(DebugInfo* info) {
		// Compiler-omitted blocks will have an address of 1. Skip these.
		if(info->block == 1) return;
		_debug_info.emplace(info->block, info);
	}
};

#endif
