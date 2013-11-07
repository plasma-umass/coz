#include "causal.h"
#include "probe.h"

__attribute__((constructor)) void ctor() {
	Causal::getInstance().initialize();
}

__attribute__((destructor)) void dtor() {
	Causal::getInstance().shutdown();
}

// The instrumentation API
extern "C" {
	void __causal_progress() {
		uintptr_t ret = (uintptr_t)__builtin_return_address(0);
		Causal::getInstance().progress(ret, (uintptr_t)__causal_progress);
	}

	void __causal_probe() {
		uintptr_t ret = (uintptr_t)__builtin_return_address(0);
		Causal::getInstance().probe(ret, (uintptr_t)__causal_probe);
	}

	void __causal_extern_enter(void* p) {
		uintptr_t ret = (uintptr_t)__builtin_return_address(0);
		Causal::getInstance().extern_enter(p, ret, (uintptr_t)__causal_extern_enter);
	}

	void __causal_extern_exit() {
		uintptr_t ret = (uintptr_t)__builtin_return_address(0);
		Causal::getInstance().extern_exit(ret, (uintptr_t)__causal_extern_exit);
	}
}

// Wrapped POSIX functions
extern "C" {
	void exit(int status) {
		static auto real_exit = (__attribute__((noreturn)) void (*)(int))dlsym(RTLD_NEXT, "exit");
		Causal::getInstance().shutdown();
		real_exit(status);
	}
	
	void _exit(int status) {
		static auto real_exit = (__attribute__((noreturn)) void (*)(int))dlsym(RTLD_NEXT, "_exit");
		Causal::getInstance().shutdown();
		real_exit(status);
	}
	
	void _Exit(int status) {
		static auto real_exit = (__attribute__((noreturn)) void (*)(int))dlsym(RTLD_NEXT, "_Exit");
		Causal::getInstance().shutdown();
		real_exit(status);
	}
	
	int fork() {
		return Causal::getInstance().fork();
	}
	
	signal_handler_t signal(int signum, signal_handler_t handler) {
		return Causal::getInstance().signal(signum, handler);
	}
	
	int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
		return Causal::getInstance().sigaction(signum, act, oldact);
	}

	int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
		return Causal::getInstance().sigprocmask(how, set, oldset);
	}

	int sigsuspend(const sigset_t* mask) {
		return Causal::getInstance().sigsuspend(mask);
	}

	int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
		return Causal::getInstance().pthread_sigmask(how, set, oldset);
	}
}

extern "C" int __real_main(int argc, char** argv);

extern "C" int main(int argc, char** argv) {
	int result = __real_main(argc, argv);
	exit(result);
}

void __causal_signal_entry(int signum, siginfo_t* info, void* p) {
	Causal::getInstance().onSignal(signum, info, p);
}
