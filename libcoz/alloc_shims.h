
#if !defined(CAUSAL_RUNTIME_ALLOC_SHIMS_H)
#define CAUSAL_RUNTIME_ALLOC_SHIMS_H

extern "C" void coz_lock_and_set_dummy_alloc_shims() __attribute__((weak));
extern "C" void coz_restore_real_alloc_shims_and_unlock() __attribute__((weak));

#endif
