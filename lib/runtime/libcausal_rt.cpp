#include "causal.h"
#include "probe.h"

// The instrumentation API
extern "C" {
	void __causal_progress() {
		uintptr_t ret = (uintptr_t)__builtin_return_address(0);
		Causal::progress(ret, (uintptr_t)__causal_progress);
	}

	void __causal_probe() {
		uintptr_t ret = (uintptr_t)__builtin_return_address(0);
		Causal::probe(ret, (uintptr_t)__causal_probe);
	}

	void __causal_extern_enter(void* p) {
		uintptr_t ret = (uintptr_t)__builtin_return_address(0);
		Causal::extern_enter(ret, (uintptr_t)__causal_extern_enter);
	}

	void __causal_extern_exit() {
		uintptr_t ret = (uintptr_t)__builtin_return_address(0);
		Causal::extern_exit(ret, (uintptr_t)__causal_extern_exit);
	}
}
