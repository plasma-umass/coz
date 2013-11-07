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
	set<pthread_t> _ignored_threads;

public:
	enum Time : uint64_t {
		Nanosecond = 1,
		Microsecond = 1000 * Nanosecond,
		Millisecond = 1000 * Microsecond,
		Second = 1000 * Millisecond
	};
	
	static void* findSymbol(const char* sym) {
		return dlsym(RTLD_DEFAULT, sym);
	}
	
	static void setSignalHandler(int signum, sigaction_handler_t handler) {
		struct sigaction sa;
		sa.sa_sigaction = handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_SIGINFO;
		CommonHost::sigaction(signum, &sa, NULL);
	}
	
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
	
	pthread_t createThread(pthread_fn_t fn, void* arg = NULL) {
		pthread_t t;
		if(pthread_create(&t, NULL, fn, arg)) {
			DEBUG("Failed to create thread");
			abort();
		}
		_ignored_threads.insert(t);
		return t;
	}
	
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
			if(pthread != NULL && _ignored_threads.find(pthread) == _ignored_threads.end()) {
				result.push_back(pthread);
			}
		}
		return result;
	}
};

typedef DarwinHost Host;

#endif
