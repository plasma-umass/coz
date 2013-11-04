#if !defined(CAUSAL_LIB_RUNTIME_DARWIN_HOST_H)
#define CAUSAL_LIB_RUNTIME_DARWIN_HOST_H

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

using namespace std;

typedef void* (*pthread_fn_t)(void*);
typedef void (*signal_handler_t)(int);
typedef void (*sigaction_handler_t)(int, siginfo_t*, void*);

enum {
	PageSize = 0x1000
};

class DarwinThread {
private:
	thread_act_t _thread;
	pthread_t _pthread;
	
public:
	DarwinThread(thread_act_t thread, pthread_t pthread) : _thread(thread), _pthread(pthread) {}
	
	bool operator==(pthread_t t) const {
		return _pthread == t;
	}
	
	bool operator!=(pthread_t t) const {
		return _pthread != t;
	}
	
	bool operator==(const DarwinThread& t) const {
		return _pthread == t._pthread;
	}
	
	bool operator!=(const DarwinThread& t) const {
		return _pthread != t._pthread;
	}
	
	void signal(int signum) {
		pthread_kill(_pthread, signum);
	}
	
	uintptr_t getPC() const {
		if(_IS_X86_64) {
			x86_thread_state64_t state;
			mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
			kern_return_t krc = thread_get_state(_thread, x86_THREAD_STATE64, (thread_state_t)&state, &count);
			if(krc != KERN_SUCCESS) {
				DEBUG("Failed to get thread PC");
				return 0;
			}
			return state.__rip;
			
		} else if(_IS_X86) {
			x86_thread_state32_t state;
			mach_msg_type_number_t count = x86_THREAD_STATE32_COUNT;
			kern_return_t krc = thread_get_state(_thread, x86_THREAD_STATE32, (thread_state_t)&state, &count);
			if(krc != KERN_SUCCESS) {
				DEBUG("Failed to get thread PC");
				return 0;
			}
			return state.__eip;
			
		} else {
			DEBUG("Unsupported architecture");
			abort();
		}
	}

	void show() const {
		if(_pthread) {
			char buf[256];
			if(pthread_getname_np(_pthread, buf, 256) == 0)
				sprintf(buf, "<%p>", _pthread);
			printf("%s: at %p\n", buf, (void*)getPC());
		} else {
			printf("mach thread: at %p\n", (void*)getPC());
		}
	}
};

class DarwinHost {
private:
	set<pthread_t> _ignored_threads;

public:
	enum Time : uint64_t {
		Nanosecond = 1,
		Millisecond = 1000 * 1000,
		Second = 1000 * Millisecond
	};
	
	typedef set<DarwinThread> ThreadsType;
	typedef DarwinThread ThreadType;
	
	static void* findSymbol(const char* sym) {
		return dlsym(RTLD_DEFAULT, sym);
	}
	
	static void setSignalHandler(int signum, sigaction_handler_t handler) {
		struct sigaction sa;
		sa.sa_sigaction = handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_SIGINFO;
		::sigaction(signum, &sa, NULL);
	}
	
	static void wait(uint64_t nanos) {
		uint64_t end_time = mach_absolute_time() + nanos;
		bool done = false;
		do {
			kern_return_t ret = mach_wait_until(end_time);
			if(ret == KERN_SUCCESS)
				done = true;
		} while(!done);
	}
	
	static size_t getTime() {
		return mach_absolute_time();
	}
	
	pthread_t createThread(pthread_fn_t fn, void* arg = NULL) {
		pthread_t t;
		if(::pthread_create(&t, NULL, fn, arg)) {
			DEBUG("Failed to create thread");
			abort();
		}
		_ignored_threads.insert(t);
		return t;
	}
	
	vector<DarwinThread> getThreads() {
		vector<DarwinThread> result;
		thread_act_array_t threads;
		mach_msg_type_number_t count;
		kern_return_t krc = task_threads(mach_task_self(), &threads, &count);
		if(krc != KERN_SUCCESS) {
			DEBUG("Failed to get task threads");
			abort();
		}
		for(size_t i=0; i<count; i++) {
			pthread_t pthread = pthread_from_mach_thread_np(threads[i]);
			if(pthread != NULL && _ignored_threads.find(pthread) == _ignored_threads.end()) {
				result.push_back(DarwinThread(threads[i], pthread));
			}
		}
		return result;
	}
	
	static int mprotectRange(uintptr_t base, uintptr_t limit, int prot) {
		base -= base % PageSize;
		limit += PageSize - 1;
		limit -= limit % PageSize;
		return mprotect((void*)base, limit - base, prot);
	}
	
	static int fork() {
		static auto real_fork = (int (*)())dlsym(RTLD_NEXT, "fork");
		return real_fork();
	}
	
	static signal_handler_t signal(int signum, signal_handler_t handler) {
		static auto real_signal = (signal_handler_t (*)(int, signal_handler_t))dlsym(RTLD_NEXT, "signal");
		return real_signal(signum, handler);
	}
	
	static int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
		static auto real_sigaction = (int (*)(int, const struct sigaction*, struct sigaction*))dlsym(RTLD_NEXT, "sigaction");
		return real_sigaction(signum, act, oldact);
	}
	
	static int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
		static auto real_sigprocmask = (int (*)(int, const sigset_t*, sigset_t*))dlsym(RTLD_NEXT, "sigprocmask");
		return real_sigprocmask(how, set, oldset);
	}
	
	static int sigsuspend(const sigset_t* mask) {
		static auto real_sigsuspend = (int (*)(const sigset_t*))dlsym(RTLD_NEXT, "sigsuspend");
		return real_sigsuspend(mask);
	}
	
	static int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
		static auto real_pthread_sigmask = (int (*)(int, const sigset_t*, sigset_t*))dlsym(RTLD_NEXT, "pthread_sigmask");
		return real_pthread_sigmask(how, set, oldset);
	}
};

typedef DarwinHost Host;

#endif
