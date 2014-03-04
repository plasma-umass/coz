#if !defined(CAUSAL_LIB_RUNTIME_BLOCK_H)
#define CAUSAL_LIB_RUNTIME_BLOCK_H

#include <cxxabi.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>

#include "debug.h"

using namespace std;

struct CallInst {
	volatile uint8_t opcode;
	volatile int32_t offset;
} __attribute__((packed));

enum { PageSize = 0x1000 };

class Probe {
private:
  static int mprotectRange(uintptr_t base, uintptr_t limit, int prot) {
		base -= base % PageSize;
		limit += PageSize - 1;
		limit -= limit % PageSize;
		return mprotect((void*)base, limit - base, prot);
	}
public:
	static void remove(uintptr_t ret, uintptr_t target = 0) {
    CallInst* c = (CallInst*)(ret - 5);
    
    mprotectRange(ret-5, ret, PROT_READ | PROT_WRITE | PROT_EXEC);
    
    uint8_t expected_opcode = 0xE8;
    uint8_t trap_opcode = 0xD6;
    uint8_t nop_opcode = 0x0f;
    if(__atomic_compare_exchange(&c->opcode, &expected_opcode, &trap_opcode,
        false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED) == true) {
      
      __atomic_store_n(&c->offset, 0x0000441f, __ATOMIC_SEQ_CST);
      __atomic_store_n(&c->opcode, nop_opcode, __ATOMIC_SEQ_CST);
    }
	}
};

#endif
