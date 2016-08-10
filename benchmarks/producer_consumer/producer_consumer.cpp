/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <cassert>

#include <queue>

#include <coz.h>

enum {
	Items = 1000000,
	QueueSize = 10,
	ProducerCount = 5,
	ConsumerCount = 3
};

int produced = 0;
int consumed = 0;
std::queue<int> queue;

pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t producer_condvar = PTHREAD_COND_INITIALIZER;
pthread_cond_t consumer_condvar = PTHREAD_COND_INITIALIZER;
pthread_cond_t main_condvar = PTHREAD_COND_INITIALIZER;

void* producer(void* arg) {
	for(size_t n = 0; n < Items / ProducerCount; n++) {
		pthread_mutex_lock(&queue_lock);
		while(queue.size() == QueueSize) {
			pthread_cond_wait(&producer_condvar, &queue_lock);
		}
    queue.push(123);
		produced++;
		pthread_mutex_unlock(&queue_lock);
		pthread_cond_signal(&consumer_condvar);
	}
	pthread_mutex_lock(&queue_lock);
	pthread_cond_signal(&main_condvar);
	pthread_mutex_unlock(&queue_lock);
	return NULL;
}

void* consumer(void* arg) {
	while(true) {
		pthread_mutex_lock(&queue_lock);
		while(queue.size() == 0) {
			pthread_cond_wait(&consumer_condvar, &queue_lock);
		}
    int front = queue.front();
    queue.pop();
		assert(front == 123);
		consumed++;
		pthread_mutex_unlock(&queue_lock);
		pthread_cond_signal(&producer_condvar);
		COZ_PROGRESS;
	}
}

int main(int argc, char** argv) {
	pthread_t producers[ProducerCount];
	pthread_t consumers[ConsumerCount];
	
	for(size_t i=0; i<ProducerCount; i++) {
		pthread_create(&producers[i], NULL, producer, NULL);
	}
	
	for(size_t i=0; i<ConsumerCount; i++) {
		pthread_create(&consumers[i], NULL, consumer, NULL);
	}
	
	pthread_mutex_lock(&queue_lock);
	while(consumed < Items) {
		pthread_cond_wait(&main_condvar, &queue_lock);
	}
	pthread_mutex_unlock(&queue_lock);
}
