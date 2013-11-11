#if !defined(CAUSAL_LIB_RUNTIME_HOST_COMMON_H)
#define CAUSAL_LIB_RUNTIME_HOST_COMMON_H

#include <dlfcn.h>

enum {
	PageSize = 0x1000
};

enum Time : uint64_t {
	ns = 1,
	us = 1000 * ns,
	ms = 1000 * us,
	s = 1000 * ms
};

typedef void* (*pthread_fn_t)(void*);
typedef void (*signal_handler_t)(int);
typedef void (*sigaction_handler_t)(int, siginfo_t*, void*);

class CommonHost {
public:
	static void setSignalHandler(int signum, sigaction_handler_t handler) {
		struct sigaction sa;
		sa.sa_sigaction = handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_SIGINFO;
		real_sigaction(signum, &sa, NULL);
	}

	static int mprotectRange(uintptr_t base, uintptr_t limit, int prot) {
		base -= base % PageSize;
		limit += PageSize - 1;
		limit -= limit % PageSize;
		return mprotect((void*)base, limit - base, prot);
	}

	static int real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
		static auto _real_pthread_create = (int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*))dlsym(RTLD_NEXT, "pthread_create");
		return _real_pthread_create(thread, attr, fn, arg);
	}

	static void real_pthread_exit(void* ret) {
		static auto _real_pthread_exit = (void (*)(void*))dlsym(RTLD_NEXT, "pthread_exit");
		_real_pthread_exit(ret);
	}
	
	static int real_fork() {
		static auto _real_fork = (int (*)())dlsym(RTLD_NEXT, "fork");
		return _real_fork();
	}
	
	static signal_handler_t real_signal(int signum, signal_handler_t handler) {
		static auto _real_signal = (signal_handler_t (*)(int, signal_handler_t))dlsym(RTLD_NEXT, "signal");
		return _real_signal(signum, handler);
	}
	
	static int real_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
		static auto _real_sigaction = (int (*)(int, const struct sigaction*, struct sigaction*))dlsym(RTLD_NEXT, "sigaction");
		return _real_sigaction(signum, act, oldact);
	}
	
	static int real_sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
		static auto _real_sigprocmask = (int (*)(int, const sigset_t*, sigset_t*))dlsym(RTLD_NEXT, "sigprocmask");
		return _real_sigprocmask(how, set, oldset);
	}
	
	static int real_sigsuspend(const sigset_t* mask) {
		static auto _real_sigsuspend = (int (*)(const sigset_t*))dlsym(RTLD_NEXT, "sigsuspend");
		return _real_sigsuspend(mask);
	}
	
	static int real_pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
		static auto _real_pthread_sigmask = (int (*)(int, const sigset_t*, sigset_t*))dlsym(RTLD_NEXT, "pthread_sigmask");
		return _real_pthread_sigmask(how, set, oldset);
	}
};

#endif
