/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 */

#include <dlfcn.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <sstream>
#include <string>
#include <unordered_set>

#include "inspect.h"
#include "profiler.h"
#include "progress_point.h"
#include "real.h"
#include "util.h"

#include "ccutil/log.h"

// Include the client header file
#include "coz.h"

using namespace std;

/// The type of a main function
typedef int (*main_fn_t)(int, char**, char**);

/// The program's real main function
main_fn_t real_main;

static bool end_to_end = false;
bool initialized = false;
static bool init_in_progress = false;

/**
 * Called by the application to get/create a progress point
 */
extern "C" coz_counter_t* _coz_get_counter(progress_point_type t, const char* name) {
  if(t == progress_point_type::throughput) {
    throughput_point* p = profiler::get_instance().get_throughput_point(name);
    if(p) return p->get_counter_struct();
    else return nullptr;
    
  } else if(t == progress_point_type::begin) {
    latency_point* p = profiler::get_instance().get_latency_point(name);
    if(p) return p->get_begin_counter_struct();
    else return nullptr;
    
  } else if(t == progress_point_type::end) {
    latency_point* p = profiler::get_instance().get_latency_point(name);
    if(p) return p->get_end_counter_struct();
    else return nullptr;
    
  } else {
    WARNING << "Unknown progress point type " << ((int)t) << " named " << name;
    return nullptr;
  }
}

/**
 * Read a link's contents and return it as a string
 */
static string readlink_str(const char* path) {
  size_t exe_size = 1024;
  ssize_t exe_used;

  while(true) {
    char exe_path[exe_size];

    exe_used = readlink(path, exe_path, exe_size - 1);
    REQUIRE(exe_used > 0) << "Unable to read link " << path;

    if(exe_used < exe_size - 1) {
      exe_path[exe_used] = '\0';
      return string(exe_path);
    }

    exe_size += 1024;
  }
}

/*
 * Initialize coz.  This will either happen as main() is called using
 * __libc_start_main, or on the first call to pthread_create() (if
 * that happen earlier, which might happen if some shared library
 * dependency create a thread in its initializer).
 */
void init_coz(void) {
  if (init_in_progress) {
    INFO << "init_coz in progress, do not recurse";
    return;
  }
  init_in_progress = true;
  INFO << "bootstrapping coz";
  initialized = false;

  // Remove Coz from LD_PRELOAD. Just clearing LD_PRELOAD for now FIXME!
  unsetenv("LD_PRELOAD");

  // Read settings out of environment variables
  string output_file = getenv_safe("COZ_OUTPUT", "profile.coz");

  vector<string> binary_scope_v = split(getenv_safe("COZ_BINARY_SCOPE"), '\t');
  unordered_set<string> binary_scope(binary_scope_v.begin(), binary_scope_v.end());

  vector<string> source_scope_v = split(getenv_safe("COZ_SOURCE_SCOPE"), '\t');
  unordered_set<string> source_scope(source_scope_v.begin(), source_scope_v.end());

  vector<string> progress_points_v = split(getenv_safe("COZ_PROGRESS_POINTS"), '\t');
  unordered_set<string> progress_points(progress_points_v.begin(), progress_points_v.end());

  end_to_end = getenv("COZ_END_TO_END");
  string fixed_line_name = getenv_safe("COZ_FIXED_LINE", "");
  int fixed_speedup;
  stringstream(getenv_safe("COZ_FIXED_SPEEDUP", "-1")) >> fixed_speedup;

  // Replace 'MAIN' in the binary_scope with the real path of the main executable
  if(binary_scope.find("MAIN") != binary_scope.end()) {
    binary_scope.erase("MAIN");
    string main_name = readlink_str("/proc/self/exe");
    binary_scope.insert(main_name);
    INFO << "Including MAIN, which is " << main_name;
  }

  // Build the memory map for all in-scope binaries
  memory_map::get_instance().build(binary_scope, source_scope);

  // Register any sampling progress points
  for(const string& line_name : progress_points) {
    /*shared_ptr<line> l = memory_map::get_instance().find_line(line_name);
    if(l) {
      progress_point* p = new sampling_progress_point(line_name, l);
      profiler::get_instance().sampling_progress_point(p);
    } else {
      WARNING << "Progress line \"" << line_name << "\" was not found.";
    }*/
    FATAL << "Sampling-based progress points are temporarily unsupported";
  }

  shared_ptr<line> fixed_line;
  if(fixed_line_name != "") {
    fixed_line = memory_map::get_instance().find_line(fixed_line_name);
    PREFER(fixed_line) << "Fixed line \"" << fixed_line_name << "\" was not found.";
  }

  // Create an end-to-end progress point and register it if running in
  // end-to-end mode
  if(end_to_end) {
    (void)profiler::get_instance().get_throughput_point("end-to-end");
  }

  // Start the profiler
  profiler::get_instance().startup(output_file,
                                   fixed_line.get(),
                                   fixed_speedup,
                                   end_to_end);

  // Synchronizations can be intercepted once the profiler has been initialized
  initialized = true;
  init_in_progress = false;
}

/**
 * Pass the real __libc_start_main this main function, then run the real main
 * function. This allows Coz to shut down when the real main function returns.
 */
static int wrapped_main(int argc, char** argv, char** env) {
  if (!initialized)
    init_coz();

  // Run the real main function
  int result = real_main(argc, argv, env);

  // Increment the end-to-end progress point just before shutdown
  if(end_to_end) {
    throughput_point* end_point =
      profiler::get_instance().get_throughput_point("end-to-end");
    end_point->visit();
  }

  // Shut down the profiler
  profiler::get_instance().shutdown();

  return result;
}

/**
 * Interpose on the call to __libc_start_main to run before libc constructors.
 */
extern "C" int __libc_start_main(main_fn_t, int, char**, void (*)(), void (*)(), void (*)(), void*) __attribute__((weak, alias("coz_libc_start_main")));

extern "C" int coz_libc_start_main(main_fn_t main_fn, int argc, char** argv,
    void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  // Find the real __libc_start_main
  auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
  // Save the program's real main function
  real_main = main_fn;
  // Run the real __libc_start_main, but pass in the wrapped main function
  int result = real_libc_start_main(wrapped_main, argc, argv, init, fini, rtld_fini, stack_end);

  return result;
}

/// Remove coz's required signals from a signal mask
static void remove_coz_signals(sigset_t* set) {
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

/// Check if a signal is required by coz
static bool is_coz_signal(int signum) {
  return signum == SampleSignal || signum == SIGSEGV || signum == SIGABRT;
}

extern "C" {
  /// Pass pthread_create calls to coz so child threads can inherit the parent's delay count
  int pthread_create(pthread_t* thread,
                     const pthread_attr_t* attr,
                     thread_fn_t fn,
                     void* arg) {
    return profiler::get_instance().handle_pthread_create(thread, attr, fn, arg);
  }

  /// Catch up on delays before exiting, possibly unblocking a thread joining this one
  void __attribute__((noreturn)) pthread_exit(void* result) {
	  profiler::get_instance().handle_pthread_exit(result);
  }

  /// Skip any delays added while waiting to join a thread
  int pthread_join(pthread_t t, void** retval) {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_join(t, retval);
    if(initialized) profiler::get_instance().post_block(true);

    return result;
  }

  int pthread_tryjoin_np(pthread_t t, void** retval) throw() {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_tryjoin_np(t, retval);
    if(initialized) profiler::get_instance().post_block(result == 0);
    return result;
  }

  int pthread_timedjoin_np(pthread_t t, void** ret, const struct timespec* abstime) {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_timedjoin_np(t, ret, abstime);
    if(initialized) profiler::get_instance().post_block(result == 0);
    return result;
  }

  /// Skip any global delays added while blocked on a mutex
  int pthread_mutex_lock(pthread_mutex_t* mutex) throw() {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_mutex_lock(mutex);
    if(initialized) profiler::get_instance().post_block(true);

    return result;
  }

  /// Catch up on delays before unblocking any threads waiting on a mutex
  int pthread_mutex_unlock(pthread_mutex_t* mutex) throw() {
    if(initialized) profiler::get_instance().catch_up();
    return real::pthread_mutex_unlock(mutex);
  }

  /// Skip any delays added while waiting on a condition variable
  int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_cond_wait(cond, mutex);
    if(initialized) profiler::get_instance().post_block(true);

    return result;
  }

  /**
   * Wait on a condvar for a fixed timeout. If the wait does *not* time out, skip any global
   * delays added during the waiting period.
   */
  int pthread_cond_timedwait(pthread_cond_t* cond,
                             pthread_mutex_t* mutex,
                             const struct timespec* time) {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_cond_timedwait(cond, mutex, time);

    // Skip delays only if the wait didn't time out
    if(initialized) profiler::get_instance().post_block(result == 0);

    return result;
  }

  /// Catchup on delays before waking a thread waiting on a condition variable
  int pthread_cond_signal(pthread_cond_t* cond) throw() {
    if(initialized) profiler::get_instance().catch_up();
    return real::pthread_cond_signal(cond);
  }

  /// Catch up on delays before waking any threads waiting on a condition variable
  int pthread_cond_broadcast(pthread_cond_t* cond) throw() {
    if(initialized) profiler::get_instance().catch_up();
    return real::pthread_cond_broadcast(cond);
  }

  /// Catch up before, and skip ahead after waking from a barrier
  int pthread_barrier_wait(pthread_barrier_t* barrier) throw() {
    if(initialized) profiler::get_instance().catch_up();
    if(initialized) profiler::get_instance().pre_block();

    int result = real::pthread_barrier_wait(barrier);

    if(initialized) profiler::get_instance().post_block(true);

    return result;
  }

  int pthread_rwlock_rdlock(pthread_rwlock_t* rwlock) throw() {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_rwlock_rdlock(rwlock);
    if(initialized) profiler::get_instance().post_block(true);
    return result;
  }

  int pthread_rwlock_timedrdlock(pthread_rwlock_t* rwlock, const struct timespec* abstime) throw() {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_rwlock_timedrdlock(rwlock, abstime);
    if(initialized) profiler::get_instance().post_block(result == 0);
    return result;
  }

  int pthread_rwlock_wrlock(pthread_rwlock_t* rwlock) throw() {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_rwlock_wrlock(rwlock);
    if(initialized) profiler::get_instance().post_block(true);
    return result;
  }

  int pthread_rwlock_timedwrlock(pthread_rwlock_t* rwlock, const struct timespec* abstime) throw() {
    if(initialized) profiler::get_instance().pre_block();
    int result = real::pthread_rwlock_timedwrlock(rwlock, abstime);
    if(initialized) profiler::get_instance().post_block(result == 0);
    return result;
  }

  int pthread_rwlock_unlock(pthread_rwlock_t* rwlock) throw() {
    if(initialized) profiler::get_instance().catch_up();
    return real::pthread_rwlock_unlock(rwlock);
  }

  /// Run shutdown before exiting
  void __attribute__((noreturn)) exit(int status) throw() {
    profiler::get_instance().shutdown();
    real::exit(status);
  }

  /// Run shutdown before exiting
  void __attribute__((noreturn)) _exit(int status) {
    profiler::get_instance().shutdown();
  	real::_exit(status);
  }

  /// Run shutdown before exiting
  void __attribute__((noreturn)) _Exit(int status) throw() {
    profiler::get_instance().shutdown();
    real::_Exit(status);
  }

  /// Don't allow programs to set signal handlers for coz's required signals
  sighandler_t signal(int signum, sighandler_t handler) throw() {
    if(is_coz_signal(signum)) {
      return NULL;
    } else {
      return real::signal(signum, handler);
    }
  }

  /// Don't allow programs to set handlers or mask signals required for coz
  int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact) throw() {
    if(is_coz_signal(signum)) {
      return 0;
    } else if(act != NULL) {
      struct sigaction my_act = *act;
      remove_coz_signals(&my_act.sa_mask);
      return real::sigaction(signum, &my_act, oldact);
    } else {
      return real::sigaction(signum, act, oldact);
    }
  }

  /// Ensure coz's signals remain unmasked
  int sigprocmask(int how, const sigset_t* set, sigset_t* oldset) throw() {
    if(how == SIG_BLOCK || how == SIG_SETMASK) {
      if(set != NULL) {
        sigset_t myset = *set;
        remove_coz_signals(&myset);
        return real::sigprocmask(how, &myset, oldset);
      }
    }

    return real::sigprocmask(how, set, oldset);
  }

  /// Ensure coz's signals remain unmasked
  int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) throw() {
    if(how == SIG_BLOCK || how == SIG_SETMASK) {
      if(set != NULL) {
        sigset_t myset = *set;
        remove_coz_signals(&myset);

        return real::pthread_sigmask(how, &myset, oldset);
      }
    }

    return real::pthread_sigmask(how, set, oldset);
  }

  /// Catch up on delays before sending a signal to the current process
  int kill(pid_t pid, int sig) throw() {
    if(pid == getpid())
      profiler::get_instance().catch_up();
    return real::kill(pid, sig);
  }

  /// Catch up on delays before sending a signal to another thread
  int pthread_kill(pthread_t thread, int sig) throw() {
    // TODO: Don't allow threads to send coz's signals
    if(initialized) profiler::get_instance().catch_up();
    return real::pthread_kill(thread, sig);
  }

  int pthread_sigqueue(pthread_t thread, int sig, const union sigval val) throw() {
    if(initialized) profiler::get_instance().catch_up();
    return real::pthread_sigqueue(thread, sig, val);
  }

  /**
   * Ensure a thread cannot wait for coz's signals.
   * If the waking signal is delivered from the same process, skip any global delays added
   * while blocked.
   */
  int sigwait(const sigset_t* set, int* sig) {
    sigset_t myset = *set;
    remove_coz_signals(&myset);
    siginfo_t info;

    if(initialized) profiler::get_instance().pre_block();

    int result = real::sigwaitinfo(&myset, &info);

    // Woken up by another thread if the call did not fail, and the waking process is this one
    if(initialized) profiler::get_instance().post_block(result != -1 && info.si_pid == getpid());

    if(result == -1) {
      // If there was an error, return the error code
      return errno;
    } else {
      // No need to check if sig is null because it's declared as non-null
      *sig = result;
      return 0;
    }
  }

  /**
   * Ensure a thread cannot wait for coz's signals.
   * If the waking signal is delivered from the same process, skip any added global delays.
   */
  int sigwaitinfo(const sigset_t* set, siginfo_t* info) {
    sigset_t myset = *set;
    siginfo_t myinfo;
    remove_coz_signals(&myset);

    if(initialized) profiler::get_instance().pre_block();

    int result = real::sigwaitinfo(&myset, &myinfo);

    // Woken up by another thread if the call did not fail, and the waking process is this one
    if(initialized) profiler::get_instance().post_block(result > 0 && myinfo.si_pid == getpid());

    if(result > 0 && info)
      *info = myinfo;

    return result;
  }

  /**
   * Ensure a thread cannot wait for coz's signals.
   * If the waking signal is delivered from the same process, skip any global delays.
   */
  int sigtimedwait(const sigset_t* set, siginfo_t* info, const struct timespec* timeout) {
    sigset_t myset = *set;
    siginfo_t myinfo;
    remove_coz_signals(&myset);

    if(initialized) profiler::get_instance().pre_block();

    int result = real::sigtimedwait(&myset, &myinfo, timeout);

    // Woken up by another thread if the call did not fail, and the waking process is this one
    if(initialized) profiler::get_instance().post_block(result > 0 && myinfo.si_pid == getpid());

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
