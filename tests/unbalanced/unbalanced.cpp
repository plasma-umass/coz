#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>
#include <queue>
#include <time.h>

#include <list>

using namespace std;

enum {
  ThreadCount = 4
};

int getNthPrime(int n) {
  list<int> primes;
  primes.push_back(2);
	int x = 3;
  while(primes.size() < n) {
    bool looks_prime = true;
    for(list<int>::iterator d = primes.begin(); looks_prime && d != primes.end(); d++)
      looks_prime &= (x % *d) != 0;
    if(looks_prime)
      primes.push_back(n);
    x += 2;
  }
  return primes.back();
}

void* thread1(void* arg) {
  getNthPrime(600);
  return NULL;
}

void* thread2(void* arg) {
  getNthPrime(700);
  return NULL;
}

int main(int argc, char** argv) {
	pthread_t threads[ThreadCount];
	for(size_t i=0; i<ThreadCount; i++) {
    if(i % 2 == 0)
      pthread_create(&threads[i], NULL, thread1, NULL);
    else
      pthread_create(&threads[i], NULL, thread2, NULL);
	}
	
	for(size_t i=0; i<ThreadCount; i++) {
		pthread_join(threads[i], NULL);
	}
	
	return 0;
}
