#if !defined(CAUSAL_RUNTIME_INSPECT_H)
#define CAUSAL_RUNTIME_INSPECT_H

#include <set>
#include <string>

/// Locate and register all basic blocks with the profiler
void registerBasicBlocks(std::set<std::string> scope);

#endif
