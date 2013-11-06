#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>
#include <queue>
#include <mach/mach_time.h>

#include <causal.h>

using namespace std;

enum {
	WorkerCount = 4,
	WorkItemCount = 100000,
	WeightA = 2,
	WeightB = 3,
	WeightC = 1
};

void work_item_A();
void work_item_B();
void work_item_C();

typedef void (*work_item_t)();
queue<work_item_t> work_queue;
pthread_mutex_t work_queue_lock = PTHREAD_MUTEX_INITIALIZER;

void* worker(void* arg) {
	while(true) {
		pthread_mutex_lock(&work_queue_lock);
		// Exit if the work queue is empty
		if(work_queue.empty()) {
			pthread_mutex_unlock(&work_queue_lock);
			return NULL;
		}
		
		// Take an item off the queue and unlock it
		work_item_t item = work_queue.front();
		work_queue.pop();
		pthread_mutex_unlock(&work_queue_lock);
		// Do work
		item();
		CAUSAL_PROGRESS;
	}
}

void wait(uint64_t nanos) {
	uint64_t end_time = mach_absolute_time() + nanos;
	bool done = false;
	do {
		kern_return_t ret = mach_wait_until(end_time);
		if(ret == KERN_SUCCESS)
			done = true;
	} while(!done);
}

void work_item_A() {
	for(int i=0; i<1000000; i++) {}
}

void work_item_B() {
	for(int i=0; i<1000000; i++) {}
}

void work_item_C() {
	for(int i=0; i<1000000; i++) {}
}

void fill_work_queue() {
	for(size_t i=0; i<WorkItemCount; i++) {
		int r = rand() % (WeightA + WeightB + WeightC);
		if(r < WeightA) {
			work_queue.push(work_item_A);
		} else if(r < WeightA + WeightB) {
			work_queue.push(work_item_B);
		} else {
			work_queue.push(work_item_C);
		}
	}
}

int main(int argc, char** argv) {
	fill_work_queue();
	
	pthread_t workers[WorkerCount];
	for(size_t i=0; i<WorkerCount; i++) {
		pthread_create(&workers[i], NULL, worker, NULL);
	}
	
	for(size_t i=0; i<WorkerCount; i++) {
		pthread_join(workers[i], NULL);
	}
	
	return 0;
}
