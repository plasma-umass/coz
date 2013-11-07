#if !defined(CAUSAL_LIB_RUNTIME_CAUSAL_H)
#define CAUSAL_LIB_RUNTIME_CAUSAL_H

#include <cxxabi.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <mutex>
#include <string>
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
	atomic<size_t> _perturb_time;
	
	mutex _progress_points_mutex;
	vector<uintptr_t> _progress_points;
	
	mutex _blocks_mutex;
	vector<uintptr_t> _blocks;
	
	pthread_t profiler_thread;
	mutex _thread_blocker;
	atomic<size_t> _thread_arrivals;
	
	Causal() {}
	
	void insertProbes(Probe* perturb) {
		pauseThreads();
		_perturb_point = perturb->getRet();
		perturb->restore();
		for(uintptr_t p : _progress_points) {
			Probe::get(p)->restore();
		}
		resumeThreads();
	}
	
	void slowdownExperiment(Probe* p, size_t delay, size_t duration) {	
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
		DEBUG("%zu progress visits, slowdown of %ld in %zu visits", _progress_visits.load(), -_perturb_time, _perturb_visits.load());
		p->addResults(real_duration, _progress_visits, -_perturb_time, _perturb_visits);
		
		_mode.store(Mode::Idle);
	}
	
	void profilerThread() {
		while(true) {
			// Sleep for 10ms
			//Host::wait(500 * Host::Time::Millisecond);

			if(_blocks.size() > 0) {
				uintptr_t p = _blocks[rand() % _blocks.size()];
				measureBlock(Probe::get(p), 500 * Host::Time::Microsecond, 50 * Host::Time::Millisecond);
				//slowdownExperiment(Probe::get(p), 500 * Host::Time::Microsecond, 50 * Host::Time::Millisecond);
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
			if(pthread_kill(thread, PauseSignal) == 0) {
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
		_perturb_time += Host::wait(_delay_size.load());
		_perturb_visits++;
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
			srand(Host::getTime());
			Host::setSignalHandler(DelaySignal, startSignalHandler);
			Host::setSignalHandler(PauseSignal, startSignalHandler);
			profiler_thread = Host::createThread(startProfilerThread);
		}
	}
	
	void shutdown() {
		if(_initialized.exchange(false)) {
			DEBUG("Shutting down");
			
			for(uintptr_t p : _blocks) {
				Probe::get(p)->showResults();
			}
		}
	}
	
	void progress(uintptr_t ret, uintptr_t target) {
		if(_mode == Mode::Idle) {
			_progress_points_mutex.lock();
			Probe* probe = Probe::get(ret, target);
			if(probe) {
				probe->remove();
				if(probe->isNew()) {
					_progress_points.push_back(ret);
				}
			}
			_progress_points_mutex.unlock();
		} else {
			_progress_visits++;
		}
	}
	
	void probe(uintptr_t ret, uintptr_t target) {
		if(_mode == Mode::Slowdown && ret == _perturb_point) {
			_perturb_visits++;
			_perturb_time += Host::wait(_delay_size.load());
			
		} else if(_mode == Mode::Speedup && ret == _perturb_point) {
			for(pthread_t thread : Host::getThreads()) {
				if(thread != pthread_self())
					pthread_kill(thread, DelaySignal);
			}
			
		} else {
			_blocks_mutex.lock();
			Probe* probe = Probe::get(ret, target);
			if(probe) {
				probe->remove();
				if(probe->isNew()) {
					_blocks.push_back(ret);
				}
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
