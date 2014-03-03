#if !defined(CAUSAL_RUNTIME_LOG_H)
#define CAUSAL_RUNTIME_LOG_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define INFO_COLOR "\033[01;34m"
#define WARNING_COLOR "\033[01;33m"
#define FATAL_COLOR "\033[01;31m"
#define SRC_COLOR "\033[34m"
#define END_COLOR "\033[0m"

#if defined(NDEBUG)
#  define LOG(color, fmt, ...) if(1) { fprintf(stderr, color fmt END_COLOR "\n", ##__VA_ARGS__); }
#  define INFO(fmt, ...)
#else
#  define LOG(color, fmt, ...) if(1) { fprintf(stderr, SRC_COLOR "[%s:%d] " color fmt END_COLOR "\n", __FILE__, __LINE__, ##__VA_ARGS__); }
#  define INFO(fmt, ...) LOG(INFO_COLOR, fmt, ##__VA_ARGS__)
#endif

#define WARNING(fmt, ...) LOG(WARNING_COLOR, fmt, ##__VA_ARGS__);
#define PREFER(cond, fmt, ...) if(!(cond)) { WARNING(fmt, ##__VA_ARGS__); }

#define FATAL(fmt, ...) if(1) { LOG(FATAL_COLOR, fmt, ##__VA_ARGS__); abort(); }
#define REQUIRE(cond, fmt, ...) if(!(cond)) { FATAL(fmt, ##__VA_ARGS__); }

#endif
