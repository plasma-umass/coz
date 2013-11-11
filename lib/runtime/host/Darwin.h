#if !defined(CAUSAL_LIB_RUNTIME_HOST_DARWIN_H)
#define CAUSAL_LIB_RUNTIME_HOST_DARWIN_H

#include <mach/mach_time.h>

#include "debug.h"
#include "host/common.h"

class DarwinHost : public CommonHost {
public:
	static size_t wait(uint64_t nanos) {
		if(nanos == 0) {
			return 0;
		}
		
		uint64_t end_time = mach_absolute_time() + nanos;
		bool done = false;
		do {
			kern_return_t ret = mach_wait_until(end_time);
			if(ret == KERN_SUCCESS)
				done = true;
		} while(!done);
		
		return nanos + mach_absolute_time() - end_time;
	}
	
	static size_t getTime() {
		return mach_absolute_time();
	}
};

typedef DarwinHost Host;

#endif
