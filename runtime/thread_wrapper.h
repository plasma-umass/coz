#if !defined(CAUSAL_RUNTIME_THREAD_WRAPPER_H)
#define CAUSAL_RUNTIME_THREAD_WRAPPER_H

typedef void* (*thread_fn_t)(void*);

/**
 * Struct to call the real thread function and pass the given argument
 */
struct thread_wrapper {
private:
  thread_fn_t _fn;
  void* _arg;
public:
  thread_wrapper(thread_fn_t fn, void* arg) : _fn(fn), _arg(arg) {}
  void* run() { return _fn(_arg); }
};

#endif
