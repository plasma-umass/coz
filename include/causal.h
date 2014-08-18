#if !defined(CAUSAL_H)
#define CAUSAL_H

#ifndef __USE_GNU
#  define __USE_GNU
#  define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <string.h> /* for memcpy hack below */

#if defined(__cplusplus)
extern "C" {
#endif

typedef void (*causal_reg_ctr_t)(int, unsigned long*, unsigned long*, const char*);

static void _causal_init_counter(int kind,
                                 unsigned long* ctr,
                                 unsigned long* backoff,
                                 const char* name) {
  void * p = dlsym(RTLD_DEFAULT, "__causal_register_counter");
  /* Hack to avoid pedantic complaint about putting function into pointer. */
  causal_reg_ctr_t reg;
  memcpy(&reg, &p, sizeof(p));
  
  if(reg)
    reg(kind, ctr, backoff, name);
}

#define CAUSAL_INCREMENT_COUNTER(kind, name) \
  if(1) { \
    static unsigned char _initialized = 0; \
    static unsigned long _global_counter = 0; \
    static __thread unsigned long _local_counter; \
    static unsigned long _backoff = 0; \
    \
    if(!_initialized) { \
      _initialized = 1; \
      _causal_init_counter(kind, &_global_counter, &_backoff, name); \
    } \
    \
    ++_local_counter; \
    if(__builtin_ctz(_local_counter) >= __atomic_load_n(&_backoff, __ATOMIC_RELAXED)) { \
      __atomic_add_fetch(&_global_counter, 1, __ATOMIC_RELAXED); \
    } \
  }

#define PROGRESS_COUNTER 1
#define BEGIN_COUNTER 2
#define END_COUNTER 3

#define STR2(x) #x 
#define STR(x) STR2(x)

#define CAUSAL_PROGRESS CAUSAL_INCREMENT_COUNTER(PROGRESS_COUNTER, __FILE__ ":" STR(__LINE__))
#define CAUSAL_BEGIN CAUSAL_INCREMENT_COUNTER(BEGIN_COUNTER, __FILE__ ":" STR(__LINE__))
#define CAUSAL_END CAUSAL_INCREMENT_COUNTER(END_COUNTER, __FILE__ ":" STR(__LINE__))

#if defined(__cplusplus)
}
#endif

#endif
