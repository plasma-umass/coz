#if !defined(COZ_H)
#define COZ_H

#ifndef __USE_GNU
#  define __USE_GNU
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <stdint.h>
#include <string.h> /* for memcpy hack below */

#if defined(__cplusplus)
extern "C" {
#endif

#define COZ_COUNTER_TYPE_THROUGHPUT 1
#define COZ_COUNTER_TYPE_BEGIN 2
#define COZ_COUNTER_TYPE_END 3

// Counter info struct, containing both a counter and backoff size
typedef struct {
  size_t count;    // The actual count
  size_t backoff;  // Used to batch updates to the shared counter. Currently unused.
} coz_counter_t;

// The type of the _coz_get_counter function
typedef coz_counter_t* (*coz_get_counter_t)(int, const char*);

// Locate and invoke _coz_get_counter
static coz_counter_t* _call_coz_get_counter(int type, const char* name) {
  static unsigned char _initialized = 0;
  coz_get_counter_t fn; // The pointer to _coz_get_counter
  
  if(!_initialized) {
    // Locate the _coz_get_counter method
    void* p = dlsym(RTLD_DEFAULT, "_coz_get_counter");
  
    // Use memcpy to avoid pedantic GCC complaint about storing function pointer in void*
    memcpy(&fn, &p, sizeof(p));
    
    _initialized = 1;
  }
  
  // Call the function, or return null if profiler is not found
  if(fn) return fn(type, name);
  else return 0;
}

// The type of the _coz_get_*_arrivals functions
typedef size_t* (*coz_get_arrivals_t)();

// Get a pointer to the thread-local arrival counter
static size_t* _call_coz_get_local_arrivals() {
  static unsigned char _initialized = 0;
  coz_get_arrivals_t fn;  // The pointer to _coz_get_local_arrivals
  
  if(!_initialized) {
    // Locate the _coz_get_local_arrivals function
    void* p = dlsym(RTLD_DEFAULT, "_coz_get_local_arrivals");
    
    // Use memcpy to avoid GCC complaints
    memcpy(&fn, &p, sizeof(p));
    
    _initialized = 1;
  }
  
  // Call the function or return null
  if(fn) return fn();
  else return 0;
}

// Get a pointer to the global arrival counter
static size_t* _call_coz_get_global_arrivals() {
  static unsigned char _initialized = 0;
  coz_get_arrivals_t fn;  // The pointer to _coz_get_local_arrivals
  
  if(!_initialized) {
    // Locate the _coz_get_local_arrivals function
    void* p = dlsym(RTLD_DEFAULT, "_coz_get_global_arrivals");
    
    // Use memcpy to avoid GCC complaints
    memcpy(&fn, &p, sizeof(p));
    
    _initialized = 1;
  }
  
  // Call the function or return null
  if(fn) return fn();
  else return 0;
}

// Macro to initialize and increment a counter
#define COZ_INCREMENT_COUNTER(type, name) \
  if(1) { \
    static unsigned char _initialized = 0; \
    static coz_counter_t* _counter = 0; \
    \
    if(!_initialized) { \
      _counter = _call_coz_get_counter(type, name); \
      _initialized = 1; \
    } \
    if(_counter) { \
      __atomic_add_fetch(&_counter->count, 1, __ATOMIC_RELAXED); \
    } \
  }

/// Macro to mark the arrival of a task.
/// Atomically increments both a count of total arrivals and the count of arrivals in this thread
#define COZ_ARRIVAL \
  if(1) { \
    static __thread unsigned int _magic; \
    static __thread size_t* _local_arrivals; \
    static __thread size_t* _global_arrivals; \
    \
    if(_magic != 0xD00FCA75) { \
      _local_arrivals = _call_coz_get_local_arrivals(); \
      _global_arrivals = _call_coz_get_global_arrivals(); \
      _magic = 0xD00FCA75; \
    } \
    \
    if(_local_arrivals && _global_arrivals) { \
      __atomic_add_fetch(_local_arrivals, 1, __ATOMIC_RELAXED); \
      __atomic_add_fetch(_global_arrivals, 1, __ATOMIC_RELAXED); \
    } \
  }

#define STR2(x) #x 
#define STR(x) STR2(x)

#define COZ_PROGRESS_NAMED(name) COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_THROUGHPUT, name)

#define COZ_PROGRESS COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_THROUGHPUT, __FILE__ ":" STR(__LINE__))
#define COZ_BEGIN(name) COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_BEGIN, name)
#define COZ_END(name) COZ_INCREMENT_COUNTER(COZ_COUNTER_TYPE_END, name)

#if defined(__cplusplus)
}
#endif

#endif
