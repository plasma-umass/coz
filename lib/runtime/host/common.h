#if !defined(CAUSAL_LIB_RUNTIME_HOST_COMMON_H)
#define CAUSAL_LIB_RUNTIME_HOST_COMMON_H

#include <dlfcn.h>

enum {
	PageSize = 0x1000
};

typedef void* (*pthread_fn_t)(void*);
typedef void (*signal_handler_t)(int);
typedef void (*sigaction_handler_t)(int, siginfo_t*, void*);

class CommonHost {
public:
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

#endif
