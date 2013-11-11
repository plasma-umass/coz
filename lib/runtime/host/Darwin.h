#if !defined(CAUSAL_LIB_RUNTIME_HOST_DARWIN_H)
#define CAUSAL_LIB_RUNTIME_HOST_DARWIN_H

#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <set>
#include <vector>

#include "arch.h"
#include "causal.h"
#include "debug.h"
#include "host/common.h"

using namespace std;

class DarwinHost : public CommonHost {
private:
	pthread_t _profiler_thread;

public:
	static void initialize() {}

	static size_t wait(uint64_t nanos) {
		if(nanos == 0) {
			return 0;
		}
		
		uint64_t end_time = mach_absolute_time() + nanos;
		bool done = false;
		do {
			kern_return_t ret = mach_wait_until(end_time);
			if(ret == KERN_SUCCESS)
				done = true;
		} while(!done);
		
		return nanos + mach_absolute_time() - end_time;
	}
	
	static size_t getTime() {
		return mach_absolute_time();
	}

	int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
		return CommonHost::real_pthread_create(thread, attr, fn, arg);
	}

	// Intercept the real_pthread_create call so we can remember to ignore the profiler thread
	int real_phread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
		pthread_t t;
		int result = CommonHost::real_pthread_create(&t, attr, fn, arg);
		if(result == 0) {
			_profiler_thread = t;
			if(thread != NULL) *thread = t;
		}
		return result;
	}
	
	void lockThreads() {}
	void unlockThreads() {}

	vector<pthread_t> getThreads() {
		// Get thread list from the kernel
		thread_act_array_t threads;
		mach_msg_type_number_t count;
		kern_return_t krc = task_threads(mach_task_self(), &threads, &count);
		if(krc != KERN_SUCCESS) {
			DEBUG("Failed to get task threads");
			abort();
		}
		
		// Build a vector to return
		vector<pthread_t> result(count);
		for(size_t i=0; i<count; i++) {
			pthread_t pthread = pthread_from_mach_thread_np(threads[i]);
			if(pthread != _profiler_thread)	result.push_back(pthread);
		}
		return result;
	}
};

typedef DarwinHost Host;

#endif
