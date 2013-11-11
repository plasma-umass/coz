#include <time.h>

#include "host/common.h"

using namespace std;

class LinuxHost : public CommonHost {
public:
	static size_t getTime() {
		struct timespec ts;
		if(clock_gettime(CLOCK_REALTIME, &ts)) {
			perror("Host::getTime():");
			abort();
		}
		return ts.tv_nsec + ts.tv_sec * Time::s;
	}

	static size_t wait(uint64_t nanos) {
		if(nanos == 0) return 0;
		size_t start_time = getTime();
		struct timespec ts;
		ts.tv_nsec = nanos % Time::s;
		ts.tv_sec = (nanos - ts.tv_nsec) / Time::s;
		while(clock_nanosleep(CLOCK_REALTIME, 0, &ts, &ts)) {}
		return getTime() - start_time;
	}
};

typedef LinuxHost Host;

