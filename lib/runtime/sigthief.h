#if !defined(CAUSAL_LIB_RUNTIME_SIGTHIEF_H)
#define CAUSAL_LIB_RUNTIME_SIGTHIEF_H

#include <signal.h>

typedef void (*signal_handler_t)(int);
typedef void (*sigaction_handler_t)(int, siginfo_t*, void*);

template<class Host, int... Signals> class SigThief;

// Base case (no signals to steal)
template<class Host> class SigThief<Host> : public Host {
public:
	signal_handler_t signal(int signum, signal_handler_t handler) {
		return Host::real_signal(signum, handler);
	}

	int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
		return Host::real_sigaction(signum, act, oldact);
	}

	int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
		return Host::real_sigprocmask(how, set, oldset);
	}

	int sigsuspend(const sigset_t* mask) {
		return Host::real_sigsuspend(mask);
	}

	int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
		return Host::real_pthread_sigmask(how, set, oldset);
	}
};

// Recursive case
template<class Host, int First, int... Rest>
class SigThief<Host, First, Rest...> : public SigThief<Host, Rest...> {
public:
	typedef SigThief<Host, Rest...> Super;
	
	signal_handler_t signal(int signum, signal_handler_t handler) {
		if(signum == First) {
			return NULL;
		} else {
			return Super::signal(signum, handler);
		}
	}
	
	int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
		if(signum == First) {
			return 0;
		} else {
			struct sigaction newact = *act;
			sigdelset(&newact.sa_mask, First);
			return Super::sigaction(signum, &newact, oldact);
		}
	}
	
	int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
		if(set != NULL) {
			sigset_t newset = *set;
			sigdelset(&newset, First);
			return Super::sigprocmask(how, &newset, oldset);
		} else {
			return Super::sigprocmask(how, set, oldset);
		}
	}
	
	int sigsuspend(const sigset_t* mask) {
		if(mask != NULL) {
			sigset_t newmask = *mask;
			sigdelset(&newmask, First);
			return Super::sigsuspend(&newmask);
		} else {
			return Super::sigsuspend(mask);
		}
	}
	
	int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
		if(set != NULL) {
			sigset_t newset = *set;
			sigdelset(&newset, First);
			return Super::pthread_sigmask(how, &newset, oldset);
		} else {
			return Super::pthread_sigmask(how, set, oldset);
		}
	}
};

#endif
