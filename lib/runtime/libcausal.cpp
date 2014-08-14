#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#include <set>
#include <string>

#include "counter.h"
#include "inspect.h"
#include "log.h"
#include "options.h"
#include "profiler.h"
#include "real.h"
#include "util.h"

using namespace std;

/// The type of a main function
typedef int (*main_fn_t)(int, char**, char**);

/// The program's real main function
main_fn_t real_main;

/**
 * Called by the application to register a progress counter
 */
extern "C" void __causal_register_counter(counter::type kind,
                                          size_t* counter,
                                          size_t* backoff,
                                          const char* name) {
  profiler::get_instance().register_counter(new source_counter(kind, counter, name));
}

/**
 * Pass the real __libc_start_main this main function, then run the real main
 * function. This allows Causal to shut down when the real main function returns.
 */
int wrapped_main(int argc, char** argv, char** env) {
  // Remove causal from LD_PRELOAD. Just clearing LD_PRELOAD for now FIXME!
  unsetenv("LD_PRELOAD");
  
  // Find the "---" separator between causal arguments and the program name
  size_t causal_argc;
  for(causal_argc = 1; causal_argc < argc && argv[causal_argc] != string("---"); causal_argc++) {
    // Do nothing
  }
  
  // If there is no separator, there must not be any causal arguments
  if(causal_argc == argc) {
    causal_argc = 1;
  }
  
  // Parse the causal command line arguments
  auto args = causal::parse_args(causal_argc, argv);
  
  // Show usage information if the help argument was passed
  if(args.count("help")) {
    causal::show_usage();
    return 1;
  }
  
  // Collect all the real function pointers for interposed functions
  real::init();
  
  // Get the profiler scope
  vector<string> scope = args["include"].as<vector<string>>();
  // If no scope was specified, use the current directory
  if(scope.size() == 0) {
    char cwd[PATH_MAX];
    getcwd(cwd, PATH_MAX);
    scope.push_back(string(cwd));
  }
  
  // Build a map of addresses to source lines
  memory_map::get_instance().build(scope, args.count("search-libs"));
  
  // Register any sampling progress points
  vector<string> progress_names = args["progress"].as<vector<string>>();
  
  for(const string& line_name : progress_names) {
    shared_ptr<line> l = memory_map::get_instance().find_line(line_name);
    if(l) {
      profiler::get_instance().register_counter(new sampling_counter(line_name, l));
    } else {
      WARNING << "Progress line \"" << line_name << "\" was not found.";
    }
  }

  string fixed_line_name = args["fixed-line"].as<string>();
  shared_ptr<line> fixed_line;
  if(fixed_line_name != "") {
    fixed_line = memory_map::get_instance().find_line(fixed_line_name);
    PREFER(fixed_line) << "Fixed line \"" << fixed_line_name << "\" was not found.";
  }
  
  // Create a phony end-to-end counter and register it if running in end-to-end mode
  end_to_end_counter c;
  if(args.count("end-to-end"))
    profiler::get_instance().register_counter(&c);
  
  // Start the profiler
  profiler::get_instance().startup(args["output"].as<string>(),
                                   fixed_line.get(),
                                   args["fixed-speedup"].as<int>(),
                                   args.count("sample-only"));
  
  // Run the real main function
  int result = real_main(argc - causal_argc - 1, &argv[causal_argc + 1], env);
  
  // Increment the end-to-end counter just before shutdown
  c.done();
  
  // Shut down the profiler
  profiler::get_instance().shutdown();
  
  return result;
}

/**
 * Interpose on the call to __libc_start_main to run before libc constructors.
 */
extern "C" int __libc_start_main(main_fn_t main_fn, int argc, char** argv,
    void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  // Find the real __libc_start_main
  auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
  // Save the program's real main function
  real_main = main_fn;
  // Run the real __libc_start_main, but pass in the wrapped main function
  int result = real_libc_start_main(wrapped_main, argc, argv, init, fini, rtld_fini, stack_end);
  
  return result;
}

/// Remove causal's required signals from a signal mask
void remove_causal_signals(sigset_t* set) {
  if(sigismember(set, SampleSignal)) {
    sigdelset(set, SampleSignal);
  }
  if(sigismember(set, SIGSEGV)) {
    sigdelset(set, SIGSEGV);
  }
  if(sigismember(set, SIGABRT)) {
    sigdelset(set, SIGABRT);
  }
}

/// Check if a signal is required by causal
bool is_causal_signal(int signum) {
  return signum == SampleSignal || signum == SIGSEGV || signum == SIGABRT;
}

extern "C" {
  /// Pass pthread_create calls to causal so child threads can inherit the parent's delay count
  int pthread_create(pthread_t* thread,
                     const pthread_attr_t* attr,
                     thread_fn_t fn,
                     void* arg) {
    return profiler::get_instance().handle_pthread_create(thread, attr, fn, arg);
  }
  
  /// Catch up on delays before exiting, possibly unblocking a thread joining this one
  void pthread_exit(void* result) {
	  profiler::get_instance().handle_pthread_exit(result);
  }
 
  /// Skip any delays added while waiting to join a thread
  int pthread_join(pthread_t t, void** retval) {
    profiler::get_instance().before_blocking();
    int result = real::pthread_join(t, retval);
    profiler::get_instance().after_unblocking(true);
    
    return result;
  }

  /// Skip any global delays added while blocked on a mutex
  int pthread_mutex_lock(pthread_mutex_t* mutex) {
    profiler::get_instance().before_blocking();
    int result = real::pthread_mutex_lock(mutex);  
    profiler::get_instance().after_unblocking(true);          
    
    return result;   
  }
  
  /// Catch up on delays before unblocking any threads waiting on a mutex
  int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    profiler::get_instance().catch_up();
    return real::pthread_mutex_unlock(mutex);
  }

  /// Skip any delays added while waiting on a condition variable
  int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    profiler::get_instance().before_blocking();
    int result = real::pthread_cond_wait(cond, mutex);       
    profiler::get_instance().after_unblocking(true);     
    
    return result;  
  }
  
  /**
   * Wait on a condvar for a fixed timeout. If the wait does *not* time out, skip any global
   * delays added during the waiting period.
   */
  int pthread_cond_timedwait(pthread_cond_t* cond,
                             pthread_mutex_t* mutex,
                             const struct timespec* time) {
    profiler::get_instance().before_blocking();
    int result = real::pthread_cond_timedwait(cond, mutex, time);

    // Skip delays only if the wait didn't time out
    profiler::get_instance().after_unblocking(result == 0);
    
    return result;
  }
  
  /// Catchup on delays before waking a thread waiting on a condition variable
  int pthread_cond_signal(pthread_cond_t* cond) {
    profiler::get_instance().catch_up();
    return real::pthread_cond_signal(cond);
  }
  
  /// Catch up on delays before waking any threads waiting on a condition variable
  int pthread_cond_broadcast(pthread_cond_t* cond) {
    profiler::get_instance().catch_up();
    return real::pthread_cond_broadcast(cond);
  }
  
  /// Catch up before, and skip ahead after waking from a barrier
  int pthread_barrier_wait(pthread_barrier_t* barrier) {
    profiler::get_instance().catch_up();
    profiler::get_instance().before_blocking();
    
    int result = real::pthread_barrier_wait(barrier);
    
    profiler::get_instance().after_unblocking(true);
    
    return result;
  }

  /// Run shutdown before exiting
  void exit(int status) {
    profiler::get_instance().shutdown();
    real::exit(status);
  }

  /// Run shutdown before exiting
  void _exit(int status) {
    profiler::get_instance().shutdown();
  	real::_exit(status);
  }

  /// Run shutdown before exiting
  void _Exit(int status) {
    profiler::get_instance().shutdown();
    real::_Exit(status);
  }

  /// Don't allow programs to set signal handlers for causal's required signals
  sighandler_t signal(int signum, sighandler_t handler) {
    if(is_causal_signal(signum)) {
      return NULL;
    } else {
      return real::signal(signum, handler);
    }
  }

  /// Don't allow programs to set handlers or mask signals required for causal
  int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) {
    if(is_causal_signal(signum)) {
      return 0;
    } else if(act != NULL) {
      struct sigaction my_act = *act;
      remove_causal_signals(&my_act.sa_mask);
      return real::sigaction(signum, &my_act, oldact);
    } else {
      return real::sigaction(signum, act, oldact);
    }
  }

  /// Ensure causal's signals remain unmasked
  int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) {
    if(how == SIG_BLOCK || how == SIG_SETMASK) {
      if(set != NULL) {
        sigset_t myset = *set;
        remove_causal_signals(&myset);
        return real::sigprocmask(how, &myset, oldset);
      }
    }
  
    return real::sigprocmask(how, set, oldset);
  }

  /// Ensure causal's signals remain unmasked
  int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
    if(how == SIG_BLOCK || how == SIG_SETMASK) {
      if(set != NULL) {
        sigset_t myset = *set;
        remove_causal_signals(&myset);
      
        return real::pthread_sigmask(how, &myset, oldset);
      }
    }
  
    return real::pthread_sigmask(how, set, oldset);
  }
  
  /// Catch up on delays before sending a signal to the current process
  int kill(pid_t pid, int sig) {
    if(pid == getpid())
      profiler::get_instance().catch_up();
    return real::kill(pid, sig);
  }
  
  /// Catch up on delays before sending a signal to another thread
  int pthread_kill(pthread_t thread, int sig) {
    // TODO: Don't allow threads to send causal's signals
    profiler::get_instance().catch_up();
    return real::pthread_kill(thread, sig);
  }
  
  /**
   * Ensure a thread cannot wait for causal's signals.
   * If the waking signal is delivered from the same process, skip any global delays added
   * while blocked.
   */
  int sigwait(const sigset_t* set, int* sig) {
    sigset_t myset = *set;
    remove_causal_signals(&myset);
    siginfo_t info;
    
    profiler::get_instance().before_blocking();
    
    int result = real::sigwaitinfo(&myset, &info);
    
    // Woken up by another thread if the call did not fail, and the waking process is this one
    profiler::get_instance().after_unblocking(result != -1 && info.si_pid == getpid());
    
    if(result == -1) {
      // If there was an error, return the error code
      return errno;
    } else {
      // If the sig pointer is not null, pass the received signal to the caller
      if(sig) *sig = result;
      return 0;
    }
  }
  
  /**
   * Ensure a thread cannot wait for causal's signals.
   * If the waking signal is delivered from the same process, skip any added global delays.
   */
  int sigwaitinfo(const sigset_t* set, siginfo_t* info) {
    sigset_t myset = *set;
    siginfo_t myinfo;
    remove_causal_signals(&myset);
    
    profiler::get_instance().before_blocking();
    
    int result = real::sigwaitinfo(&myset, &myinfo);
    
    // Woken up by another thread if the call did not fail, and the waking process is this one
    profiler::get_instance().after_unblocking(result > 0 && myinfo.si_pid == getpid());
    
    if(result > 0 && info)
      *info = myinfo;
    
    return result;
  }
  
  /**
   * Ensure a thread cannot wait for causal's signals.
   * If the waking signal is delivered from the same process, skip any global delays.
   */
  int sigtimedwait(const sigset_t* set, siginfo_t* info, const struct timespec* timeout) {
    sigset_t myset = *set;
    siginfo_t myinfo;
    remove_causal_signals(&myset);
    
    profiler::get_instance().before_blocking();
    
    int result = real::sigtimedwait(&myset, &myinfo, timeout);
    
    // Woken up by another thread if the call did not fail, and the waking process is this one
    profiler::get_instance().after_unblocking(result > 0 && myinfo.si_pid == getpid());
    
    if(result > 0 && info)
      *info = myinfo;
    
    return result;
  }
  
  /**
   * Set the process signal mask, suspend, then wake and restore the signal mask.
   * If the waking signal is delivered from within the process, skip any added global delays
   */
  int sigsuspend(const sigset_t* set) {
    sigset_t oldset;
    int sig;
    real::sigprocmask(SIG_SETMASK, set, &oldset);
    int rc = sigwait(set, &sig);
    real::sigprocmask(SIG_SETMASK, &oldset, nullptr);
    return rc;
  }
}
