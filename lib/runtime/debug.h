#if !defined(DEBUG_HPP)
#define DEBUG_HPP

#if defined(NDEBUG)
#define DEBUG(...)
#endif

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#define DEBUG(...) \
	{ \
		fprintf(stderr, " [%d]: ", getpid()); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
	}

#endif
