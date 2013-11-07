#if !defined(CAUSAL_LIB_RUNTIME_PROBE_H)
#define CAUSAL_LIB_RUNTIME_PROBE_H

#include <cxxabi.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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

struct Experiment {
	size_t _time;
	size_t _progress_count;
	size_t _total_delay;
	size_t _perturb_count;
	
	Experiment(size_t time, size_t progress_count, size_t total_delay, size_t perturb_count) :
		_time(time), _progress_count(progress_count), _total_delay(total_delay), _perturb_count(perturb_count) {}	
};

struct Probe {
private:
	atomic<bool> _is_new = ATOMIC_VAR_INIT(true);
	uintptr_t _base;
	uintptr_t _ret;
	bool _in_place = true;
	CallInst _saved;
	string _name;
	vector<Experiment> _results;
	
	Probe(uintptr_t base, uintptr_t ret) : _base(base), _ret(ret) {
		if(Host::mprotectRange(_base, _ret, PROT_READ | PROT_WRITE | PROT_EXEC)) {
			perror("Error un-protecting memory");
			fprintf(stderr, "called mprotect(%p, %p)\n", (void*)_base, (void*)_ret);
			abort();
		}
	}

public:
	static Probe* get(uintptr_t ret, uintptr_t target = 0) {
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
	
	static bool removeProbe(uintptr_t ret, uintptr_t target) {
		Probe* p = Probe::get(ret, target);
		if(p) {
			p->remove();
			return true;
		} else {
			return false;
		}
	}
	
	static bool restoreProbe(uintptr_t ret) {
		Probe* p = Probe::get(ret);
		if(p) {
			p->restore();
			return true;
		} else {
			return false;
		}
	}
	
	bool isNew() {
		return _is_new.exchange(false);
	}
	
	uintptr_t getBase() {
		return _base;
	}
	
	uintptr_t getRet() {
		return _ret;
	}
	
	void addResults(size_t time, size_t progress_count, size_t total_delay = 0, size_t delay_count = 0) {
		_results.push_back(Experiment(time, progress_count, total_delay, delay_count));
	}
	
	float getBaselineRate() {
		size_t total_time = 0;
		size_t total_count = 0;
		for(const auto& e : _results) {
			if(e._total_delay == 0) {
				total_time += e._time;
				total_count += e._progress_count;
			}
		}
		return (float)total_time / (float)total_count;
	}
	
	float getMarginalImpact() {
		size_t total_time = 0;
		size_t total_count = 0;
		size_t total_delay = 0;
		size_t perturb_count = 0;
		
		for(const auto& e : _results) {
			if(e._total_delay > 0) {
				total_time += e._time;
				total_count += e._progress_count;
				total_delay += e._total_delay;
				perturb_count += e._perturb_count;
			}
		}
		
		float rate = (float)total_time / (float)total_count;
		float slowdown = rate - getBaselineRate();
		float avg_delay = (float)total_delay / (float)perturb_count;
		return slowdown / avg_delay;
	}
	
	const string& getName() {
		if(_name.length() > 0)
			return _name;
		
		Dl_info info;
		if(dladdr((void*)getBase(), &info) == 0) {
			char buf[128];
			sprintf(buf, "<%p>", (void*)getBase());
			_name = buf;
		} else {
			char* demangled = abi::__cxa_demangle(info.dli_sname, NULL, NULL, NULL);
			if(demangled != NULL) {
				_name = demangled;
				free(demangled);
			} else {
				_name = info.dli_sname;
			}
			size_t offset = getBase() - (uintptr_t)info.dli_saddr;
			char buf[128];
			sprintf(buf, " + %zu", offset);
			_name += buf;
		}
		return _name;
	}
	
	void showResults() {
		if(_results.size() > 0) {
			float impact = getMarginalImpact();
			if(impact != 0 && !isnan(impact))
				fprintf(stderr, "%s: %f\n", getName().c_str(), getMarginalImpact());
		}
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
