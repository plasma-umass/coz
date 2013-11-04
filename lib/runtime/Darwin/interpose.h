#if !defined(CAUSAL_LIB_RUNTIME_DARWIN_INTERPOSE_H)
#define CAUSAL_LIB_RUNTIME_DARWIN_INTERPOSE_H

struct _interpose {
	void *new_func;
	void *orig_func;
};

#define INTERPOSE(my_fn, real_fn) \
	static const _interpose interpose_##real_fn __attribute__((used, section("__DATA, __interpose"))) = {\
	        (void*)my_fn, \
	        (void*)real_fn \
	}

#endif
