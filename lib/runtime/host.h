#if !defined(CAUSAL_LIB_RUNTIME_HOST_H)
#define CAUSAL_LIB_RUNTIME_HOST_H

#if defined(__APPLE__)
#	include "Darwin/host.h"
#elif defined(__linux__)
#	include "Linux/host.h"
#else
#	error "Unsupported host platform"
#endif

#endif
