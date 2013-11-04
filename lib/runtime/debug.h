#if !defined(DEBUG_HPP)
#define DEBUG_HPP

#if defined(NDEBUG)
#define DEBUG_LEVEL 4
#elif !defined(DEBUG_LEVEL)
#define DEBUG_LEVEL 0
#endif

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#define DEBUG(level, ...) \
	if(level >= DEBUG_LEVEL) { \
		fprintf(stderr, " [%s:%d] %d: ", __FILE__, __LINE__, getpid()); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
	}

#endif
