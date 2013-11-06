#if !defined(CAUSAL_LIB_RUNTIME_PROBE_H)
#define CAUSAL_LIB_RUNTIME_PROBE_H

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <atomic>
#include <map>
#include <mutex>

#include "util.h"

using namespace std;

struct CallInst {
	volatile uint8_t opcode;
	volatile int32_t offset;
} __attribute__((packed));

static uintptr_t find_call(uintptr_t ret, uintptr_t dest) {
	uint8_t* p = (uint8_t*)ret;
	if(p[-5] == 0xE8 && ret + *(int32_t*)&p[-4] == dest) {
		return ret - 5;
	} else {
		return 0;
	}
}

struct Probe {
private:
	uintptr_t _base;
	uintptr_t _ret;
	bool _in_place;
	CallInst _saved;
	
	Probe(uintptr_t base, uintptr_t ret) : 
			_base(base), _ret(ret), _in_place(true) {
		if(Host::mprotectRange(_base, _ret, PROT_READ | PROT_WRITE | PROT_EXEC)) {
			perror("Error un-protecting memory");
			fprintf(stderr, "called mprotect(%p, %p)\n", (void*)_base, (void*)_ret);
			abort();
		}
	}

public:
	static Probe* get(uintptr_t ret, uintptr_t target) {
		static map<uintptr_t, Probe*> probes;
		static mutex m;
		m.lock();
		if(probes.find(ret) == probes.end()) {
			uintptr_t call = find_call(ret, target);
			if(call == 0)
				probes[ret] = NULL;
			else
				probes[ret] = new Probe(call, ret);
		}
		Probe* p = probes[ret];
		m.unlock();
		return p;
	}
	
	uintptr_t getBase() {
		return _base;
	}
	
	uintptr_t getRet() {
		return _ret;
	}
	
	void remove() {
		if(_in_place) {
			CallInst* p = (CallInst*)_base;
			_saved = *p;
			p->opcode = 0xCC;
			p->offset = 0x0000441f;
			p->opcode = 0x0f;
			_in_place = false;
		}
	}
	
	void restore() {
		if(!_in_place) {
			CallInst* p = (CallInst*)_base;
			p->offset = _saved.offset;
			p->opcode = _saved.opcode;
			_in_place = true;
		}
	}
};

#endif
