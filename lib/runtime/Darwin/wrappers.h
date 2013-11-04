#if !defined(CAUSAL_LIB_RUNTIME_DARWIN_WRAPPERS_H)
#define CAUSAL_LIB_RUNTIME_DARWIN_WRAPPERS_H

#include <pthread.h>
#include <signal.h>

#include "causal.h"
#include "interpose.h"

int wrapped_fork() {
	INTERPOSE(wrapped_fork, fork);
	return Causal::getInstance().fork();
}

int wrapped_execve(const char* filename, char* const argv[], char* const envp[]) {
	INTERPOSE(wrapped_execve, execve);
	Causal::getInstance().shutdown();
	int status = execve(filename, argv, envp);
	// If execve returned, something bad happend. Re-init causal and return.
	Causal::getInstance().initialize();
	return status;
}

void wrapped_exit(int status) {
	INTERPOSE(wrapped_exit, exit);
	Causal::getInstance().shutdown();
	exit(status);
}

void wrapped_Exit(int status) {
	INTERPOSE(wrapped_Exit, _Exit);
	Causal::getInstance().shutdown();
	_Exit(status);
}

signal_handler_t wrapped_signal(int signum, signal_handler_t handler) {
	INTERPOSE(wrapped_signal, signal);
	return Causal::getInstance().signal(signum, handler);
}

int wrapped_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
	INTERPOSE(wrapped_sigaction, sigaction);
	return Causal::getInstance().sigaction(signum, act, oldact);
}

int wrapped_sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
	INTERPOSE(wrapped_sigprocmask, sigprocmask);
	return Causal::getInstance().sigprocmask(how, set, oldset);
}

int wrapped_sigsuspend(const sigset_t* mask) {
	INTERPOSE(wrapped_sigsuspend, sigsuspend);
	return Causal::getInstance().sigsuspend(mask);
}

int wrapped_pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
	INTERPOSE(wrapped_pthread_sigmask, pthread_sigmask);
	return Causal::getInstance().pthread_sigmask(how, set, oldset);
}

#endif
