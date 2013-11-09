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

#include "util.h"

using namespace std;

struct CallInst {
	volatile uint8_t opcode;
	volatile int32_t offset;
} __attribute__((packed));

class Probe {
private:
	uintptr_t _base;	//< The base address of the instrumentation call
	uintptr_t _ret;		//< The return address from the instrumentation call
	CallInst _saved;	//< The saved call instruction
	
	/// Private constructor
	Probe(uintptr_t base, uintptr_t ret) : _base(base), _ret(ret) {
		_saved = *(CallInst*)base;
		if(Host::mprotectRange(_base, _ret, PROT_READ | PROT_WRITE | PROT_EXEC)) {
			perror("Error un-protecting memory");
			abort();
		}
	}
	
public:
	static Probe& get(uintptr_t ret, uintptr_t target = 0) {
		static map<uintptr_t, Probe*> _probes;
		static mutex _probes_mutex;
		
		// Double-check locking is okay, because values are only ever inserted.
		if(_probes.find(ret) == _probes.end()) {
			_probes_mutex.lock();
			if(_probes.find(ret) == _probes.end()) {
				// Look for the call instruction
				uint8_t* p = (uint8_t*)ret;
				if(p[-5] == 0xE8) {
					// Found the call. Validate the offset if a call target was provided
					assert(target == 0 || ret + *(int32_t*)&p[-4] == target);
					_probes[ret] = new Probe(ret - 5, ret);
				} else {
					DEBUG("Unable to locate call instruction");
					abort();
				}
			}
			_probes_mutex.unlock();
		}
		Probe* p = _probes[ret];
		return *p;
	}
	
	operator uintptr_t() {
		return _ret;
	}
	
	/// Use dynamic loader information to locate symbols for this block
	string getName() {
		string result;
		Dl_info info;
		if(dladdr((void*)_ret, &info) == 0) {
			char buf[19];
			sprintf(buf, "<%p>", (void*)_ret);
			result = buf;
			
		} else {
			// A symbol was found. Try to demangle it
			char* demangled = abi::__cxa_demangle(info.dli_sname, NULL, NULL, NULL);
			if(demangled != NULL) {
				// Use the demangled name
				result = demangled;
				free(demangled);
			} else {
				// Just use the raw symbol name
				result = info.dli_sname;
			}
			// Add an offset (in bytes) to the symbol to uniquely identify blocks
			size_t offset = _ret - (uintptr_t)info.dli_saddr;
			char buf[128];
			sprintf(buf, " + %zu", offset);
			result += buf;
		}
		
		return result;
	}
	
	void remove() {
		CallInst* c = (CallInst*)_base;
		if(__sync_val_compare_and_swap(&c->opcode, _saved.opcode, 0xcc) == _saved.opcode) {
			c->offset = 0x0000441f;
			__sync_synchronize();
			c->opcode = 0x0f;
		}
	}
	
	void restore() {
		CallInst* c = (CallInst*)_base;
		if(__sync_val_compare_and_swap(&c->opcode, 0x0f, 0xcc) == 0x0f) {
			c->offset = _saved.offset;
			__sync_synchronize();
			c->opcode = _saved.opcode;
		}
	}
};

#endif
