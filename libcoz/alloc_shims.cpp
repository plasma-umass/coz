
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "perf.h"
#include "ccutil/spinlock.h"

static std::atomic_bool initialized{false};
static spinlock init_lock;
static spinlock mode_lock;
static std::atomic<pid_t> thread_using_shim{0};

static constexpr size_t memory_pool_size = 1000 * alignof(std::max_align_t);
alignas(std::max_align_t) static char memory_pool[memory_pool_size];

static void lazy_init();

static void* first_malloc(size_t size) {
  lazy_init();
  return malloc(size);
}

static void* first_calloc(size_t nmemb, size_t size) {
  lazy_init();
  return calloc(nmemb, size);
}

static void* (*in_use_malloc)(size_t size) = first_malloc;
static void* (*in_use_calloc)(size_t nmemb, size_t size) = first_calloc;

static void* (*real_malloc)(size_t size) = nullptr;
static void (*real_free)(void* ptr) = nullptr;
static void* (*real_calloc)(size_t nmemb, size_t size) = nullptr;

static void* dummy_malloc(size_t size) {
  // We use dummy malloc only in thread requesting it, during resolution of real
  // symbols. Other threads during that time should use real malloc.
  if (gettid() != thread_using_shim.load()) {
    // Only possible case when real_malloc is nullptr, is when we're acctually
    // looking for it right now, in another thread (initialization is in
    // progress in another thread). So it is highly unlikly to happen, but still
    // we better be sure. Simplest thing we can do is use busy waiting, since
    // it should almost never happen.
    while (!initialized.load()) {
      /* busy wait */
    }
    return real_malloc(size);
  }

  // Dummy malloc is used only during resolving real symbols by coz.
  // For that particular case, we don't need sofisticated memory allocation
  // algorithm or a lot of memory at our disposal.
  // However, we should ensure as much correctness of the algorithm as possible,
  // like memory alignment, and non overlapping buffers.

  static char* first_unallocated{memory_pool};

  // Make size multiple of alignof(max_align_t), to keep addresses aligned.
  constexpr std::uintmax_t all_ones = ~(std::uintmax_t{});
  size = (size + alignof(std::max_align_t) - 1) &
      (all_ones * alignof(std::max_align_t)); // this is same as shifting left.

  char* result = first_unallocated;
  first_unallocated += size;

  if (first_unallocated > &memory_pool[memory_pool_size])
    abort();

  return result;
}

static void* dummy_calloc(size_t nmemb, size_t size) {
  void* ptr = dummy_malloc(nmemb * size);
  memset(ptr, 0, nmemb * size);
  return ptr;
}

static void set_dummy_allocs_impl() {
  // If another thread ends up in dummy_malloc, make sure it knows it's in the
  // wrong place.
  thread_using_shim.store(gettid());
  in_use_malloc = dummy_malloc;
  in_use_calloc = dummy_calloc;
}

static void restore_real_allocs_impl() {
  in_use_malloc = real_malloc;
  in_use_calloc = real_calloc;
  thread_using_shim.store(0);
}

static void find_real_functions() {
  // Dummy allocs are on by default, so it is safe to call dlsym.
  real_malloc = reinterpret_cast<void* (*)(size_t)>(dlsym(RTLD_NEXT, "malloc"));
  if (!real_malloc) {
    fprintf(stderr, "Failed to find real malloc!\n");
    abort();
  }

  real_free = reinterpret_cast<void (*)(void*)>(dlsym(RTLD_NEXT, "free"));
  if (!real_free) {
    fprintf(stderr, "Failed to find real free!\n");
    abort();
  }

  real_calloc =
      reinterpret_cast<void* (*)(size_t, size_t)>(dlsym(RTLD_NEXT, "calloc"));
  if (!real_calloc) {
    fprintf(stderr, "Failed to find real calloc!\n");
    abort();
  }
}

static void lazy_init() {
  // First check to improve performance, avoid locking if unnecessary.
  if (initialized.load(std::memory_order_acquire))
    return;

  init_lock.lock();

  // Another check, this time to make sure no one acquired the lock between
  // first check and our attempt to acquire it.
  if (initialized.load(std::memory_order_relaxed))
    return;

  // Allocations could be made by libdl while we search for real functions.
  // Prepare dummy allocs for this (first_malloc is no longer needed, we are
  // already initializing).
  set_dummy_allocs_impl();

  // Now find real functions.
  find_real_functions();

  // Now that we have real functions, use them by default.
  restore_real_allocs_impl();

  initialized.store(true, std::memory_order_release);
  init_lock.unlock();
}

extern "C" {
void coz_lock_and_set_dummy_alloc_shims() {
  lazy_init();

  // This is to make sure, only one thread resolves real symbols at the time.
  // That in turn makes it possible, to ensure dummy malloc is called only
  // from the thread that resolves real symbol, while it resolves it.
  // Waiting for this lock should be extrimely rare case. Symbols are usually
  // resolved from one thread, and each resolve_* function is called only once,
  // during runtime of the program.
  mode_lock.lock();
  set_dummy_allocs_impl();
}

void coz_restore_real_alloc_shims_and_unlock() {
  restore_real_allocs_impl();
  mode_lock.unlock();
}

void* malloc(size_t size) {
  // When dummy implementation is not needed (during most of the runtime of
  // the program), shim will directly call real implementations, minimizing
  // overhead as much as teoretically possible.
  return in_use_malloc(size);
}

void free(void *ptr) {
  // Null ptrs are ignored anyway.
  if (!ptr)
    return;

  // If it is allocated in our pool we should free it ( or not :) )
  if (ptr >= memory_pool && ptr < memory_pool + memory_pool_size)
    return;

  // It is probably allocated with real malloc, so let real free deal with it.
  real_free(ptr);
}

void* calloc(size_t nmemb, size_t size) {
  return in_use_calloc(nmemb, size);
}

}