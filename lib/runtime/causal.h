#if !defined(CAUSAL_LIB_RUNTIME_CAUSAL_H)
#define CAUSAL_LIB_RUNTIME_CAUSAL_H

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <vector>

#include "host.h"
#include "probe.h"
#include "sigthief.h"

using namespace std;

struct Causal : public SigThief<Host, SIGUSR1, SIGUSR2> {
private:
	pthread_t profiler_thread;
	
	pthread_mutex_t blocks_lock = PTHREAD_MUTEX_INITIALIZER;
	vector<Probe*> blocks;

	uintptr_t perturbed_point = 0;
	size_t delay_size = 0;
	
	atomic<size_t> progress_count;
	atomic<size_t> perturbed_count;
	
	Causal() {
		initialize();
	}
	
	static void* startProfilerThread(void* arg) {
		getInstance().profilerThread();
		return NULL;
	}
	
	void slowdownExperiment(Probe* p, size_t delay, size_t duration, size_t min_trips = 10, size_t max_retries = 10) {	
		// Measure the baseline progress rate
		progress_count.store(0);
		Host::wait(duration);
		size_t control_count = progress_count.load();
		
		// Measure the perturbed progress rate
		delay_size = delay;
		perturbed_point = p->getRet();
		p->restore();
		progress_count.store(0);
		perturbed_count.store(0);
		Host::wait(duration);
		perturbed_point = 0;
		//p->remove();
		size_t treatment_count = progress_count.load();
		size_t num_perturbs = perturbed_count.load();
		
		if(num_perturbs < min_trips || control_count < min_trips || treatment_count < min_trips) {
			if(num_perturbs > 0 && max_retries > 0) {
				slowdownExperiment(p, delay, duration * 2, min_trips, max_retries - 1);
			}
		} else {
			float control_progress_period = (float)duration / (float)control_count;
			float treatment_progress_period = (float)duration / (float)treatment_count;
			float treatment_perturb_period = (float)duration / (float)num_perturbs;
			float control_perturb_period = treatment_perturb_period - delay;
			
			printf("%p: %f %f\n", (void*)p->getBase(), (float)delay, treatment_progress_period - control_progress_period);
		}
	}
	
	void profilerThread() {
		while(true) {
			// Sleep for 10ms
			Host::wait(10 * Host::Time::Millisecond);
			
			if(blocks.size() > 0) {
				Probe* p = blocks[rand() % blocks.size()];
				slowdownExperiment(p, 1000 * Host::Time::Nanosecond, 100 * Host::Time::Millisecond);
			}
		}
	}

public:
	static Causal& getInstance() {
		static char buf[sizeof(Causal)];
		static Causal* instance = new(buf) Causal();
		return *instance;
	}
	
	void initialize() {
		profiler_thread = Host::createThread(startProfilerThread);
	}
	
	/*void progress() {
		progress_count++;
	}
	
	void probe(uintptr_t ret, uintptr_t called_fn) {
		if(ret == perturbed_point) {
			perturbed_count++;
			real_sleep(delay_size);
		} else {
			pthread_mutex_lock(&blocks_lock);
			Probe* probe = Probe::get(ret, called_fn);
			if(probe) {
				blocks.push_back(probe);
				probe->remove();
			}
			pthread_mutex_unlock(&blocks_lock);
		}
	}*/
	
	static void progress(uintptr_t ret, uintptr_t target) {
		Probe* probe = Probe::get(ret, target);
		if(probe)
			probe->remove();
	}
	
	static void probe(uintptr_t ret, uintptr_t target) {
		Probe* probe = Probe::get(ret, target);
		if(probe)
			probe->remove();
	}
	
	static void extern_enter(uintptr_t ret, uintptr_t target) {
		Probe* probe = Probe::get(ret, target);
		if(probe)
			probe->remove();
	}
	
	static void extern_exit(uintptr_t ret, uintptr_t target) {
		Probe* probe = Probe::get(ret, target);
		if(probe)
			probe->remove();
	}
};

#endif
