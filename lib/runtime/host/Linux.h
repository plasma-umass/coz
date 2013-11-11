#include <time.h>

#include <mutex>
#include <set>

#include "host/common.h"

using namespace std;

class LinuxHost : public CommonHost {
private:
	set<pthread_t> _threads;
	mutex _threads_mutex;

	class ThreadInit {
	private:
		LinuxHost* _host;
		void* (*_fn)(void*);
		void* _arg;
	public:
		ThreadInit(LinuxHost* host, void* (*fn)(void*), void* arg) : _host(host), _fn(fn), _arg(arg) {}

		void run() {
			void* result = _fn(_arg);
			_host->pthread_exit(result);
		}

		static void* entry(void* arg) {
			ThreadInit* init = (ThreadInit*)arg;
			init->run();
			return NULL; // Unreachable
		}
	};

public:
	void initialize() {
		// Add the main thread
		_threads.insert(pthread_self());
	}

	int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
		ThreadInit* init = new ThreadInit(this, fn, arg);
		pthread_t thread_id;
		int result = CommonHost::real_pthread_create(&thread_id, attr, ThreadInit::entry, init); 
		if(result == 0) {
			_threads_mutex.lock();
			_threads.insert(thread_id);
			_threads_mutex.unlock();
			if(thread != NULL) *thread = thread_id;
		}
		return result;
	}

	void pthread_exit(void* ret) {
		_threads_mutex.lock();
		_threads.erase(pthread_self());
		_threads_mutex.unlock();
		CommonHost::real_pthread_exit(ret);
	}

	void lockThreads() {
		_threads_mutex.lock();
	}

	void unlockThreads() {
		_threads_mutex.unlock();
	}

	set<pthread_t>& getThreads() {
		return _threads;
	}

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

