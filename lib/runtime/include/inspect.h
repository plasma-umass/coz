#if !defined(CAUSAL_RUNTIME_INSPECT_H)
#define CAUSAL_RUNTIME_INSPECT_H

#include <map>
#include <set>
#include <string>

#include "basic_block.h"
#include "interval.h"

/// Locate and register all basic blocks with the profiler
void inspectExecutables(std::set<std::string> patterns, bool include_all = false);

/// Locate a basic block containing a specific address
basic_block* findBlock(uintptr_t p);

/// Locate a basic block by name (using a symbol with offset or block index)
basic_block* findBlock(std::string s);

/// Access the block info array
const std::map<interval, basic_block*>& getBlocks();

#endif
