#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>
#include <queue>
#include <time.h>

#include <causal.h>

using namespace std;

enum {
	WorkerCount = 8,
	WorkItemCount = 10000,
	WeightA = 3,
	WeightB = 2,
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

void work_item_A() {
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000 * 1000;
	//ts.tv_nsec = 800000; // Subtracted 200000ns
	while(nanosleep(&ts, &ts) != 0) {}
}

void work_item_B() {
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000 * 1000;
	while(nanosleep(&ts, &ts) != 0) {}
}

void work_item_C() {
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000 * 1000;
	while(nanosleep(&ts, &ts) != 0) {}
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
