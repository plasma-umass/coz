/*
 * Copyright (c) 2015, Charlie Curtsinger and Emery Berger,
 *                     University of Massachusetts Amherst
 * This file is part of the Coz project. See LICENSE.md file at the top-level
 * directory of this distribution and at http://github.com/plasma-umass/coz.
 *
 * A JVMTI agent that performs causal profiling of Java code.
 *
 * Why this is not just a binding
 * ------------------------------
 * The Rust, Go and Swift bindings hand their progress points to libcoz and let
 * it do the work: libcoz samples the program counter, maps it to a source line
 * through DWARF, and inserts virtual delays by pausing native threads. None of
 * that survives contact with the JVM. Java methods are compiled at run time
 * into anonymous memory with no DWARF, so a sampled PC maps to nothing, and
 * the threads to delay are Java threads that libcoz has never heard of.
 *
 * So this agent reimplements the causal profiling loop in JVM terms:
 *
 *   - Sampling. Every sample period the agent asks JVMTI for the top frame of
 *     every Java thread (GetAllStackTraces) and resolves it to a source line
 *     with GetLineNumberTable. JCoz instead used AsyncGetCallTrace, a private,
 *     undocumented HotSpot entry point; GetAllStackTraces is specified, stable
 *     and works on every JDK and both platforms.
 *
 *   - Virtual speedup. To pretend that one line runs faster, the agent slows
 *     everything else down: for each sample that landed on the selected line,
 *     it owes every *other* thread a pause of `speedup * sample_period`. It
 *     pays that debt with SuspendThreadList / ResumeThreadList, batched so the
 *     JVM only reaches a safepoint once per batch rather than once per sample.
 *
 *   - Progress points. coz.Coz.progress()/begin()/end() land in the natives
 *     below and bump a counter. The experiment loop reads the counters before
 *     and after each experiment; the delta is the throughput measurement.
 *
 * Output is coz's own JSON Lines profile, so `coz plot` reads it unchanged.
 *
 * Locking discipline
 * ------------------
 * The agent suspends Java threads while they are at arbitrary points in their
 * execution. If a suspended thread held a lock that the agent then tried to
 * take, the agent would deadlock the whole VM. So no lock is ever shared with
 * a Java thread: the progress-point registry is an append-only, lock-free list
 * that Java threads only ever push onto and the agent only ever walks. Every
 * other data structure here is touched by the agent thread alone.
 */

#include <jni.h>
#include <jvmti.h>

#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef __APPLE__
#include <sys/syscall.h>
#endif

#include <dlfcn.h>
#include <setjmp.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Tunables, mirroring libcoz/profiler.h
// ---------------------------------------------------------------------------

/// Nanoseconds between samples. Each sample is a GetAllStackTraces call, which
/// brings the VM to a safepoint, so this is far more expensive than libcoz's
/// 1ms PC sample. 5ms keeps overhead tolerable while still collecting enough
/// samples to pick a line.
constexpr int64_t kDefaultSamplePeriod = 5'000'000;

/// Samples per delay batch. Delays are accumulated across a batch and paid in
/// one suspend/resume round, so the VM reaches a safepoint for delay insertion
/// once per batch instead of once per sample.
constexpr int kSampleBatchSize = 10;

/// Virtual speedups are chosen from {0/20, 1/20, ... 20/20}.
constexpr int kSpeedupDivisions = 20;

/// Extra weight given to the 0% baseline, so roughly half of all experiments
/// measure the un-sped-up program.
constexpr int kZeroSpeedupWeight = 7;

/// Minimum wall time for one experiment.
constexpr int64_t kExperimentMinTime = 500'000'000;

/// Idle time between experiments, letting the previous speedup wash out.
constexpr int64_t kExperimentCoolOff = 100'000'000;

/// An experiment is only worth recording if every progress point advanced at
/// least this much; otherwise the throughput estimate is noise.
constexpr int64_t kExperimentTargetDelta = 5;

/// Give up extending an experiment after this long, even if the progress point
/// never reaches kExperimentTargetDelta. Without this a program whose progress
/// point stops firing would hang the experiment loop until VM death.
constexpr int64_t kExperimentMaxTime = 5'000'000'000;

// ---------------------------------------------------------------------------
// Progress points: an append-only, lock-free list
// ---------------------------------------------------------------------------

enum CounterKind { kThroughput = 1, kBegin = 2, kEnd = 3 };

struct ProgressPoint {
  std::string name;
  CounterKind kind;
  std::atomic<int64_t> count{0};
  ProgressPoint* next = nullptr;

  ProgressPoint(std::string n, CounterKind k) : name(std::move(n)), kind(k) {}
};

std::atomic<ProgressPoint*> g_points{nullptr};

/// Find or create the counter for (name, kind).
///
/// Called from Java threads. Racing creators may both allocate; the loser's
/// node is dropped and the winner's returned, so a given (name, kind) always
/// resolves to one counter. Nodes are never freed -- the list lives as long as
/// the VM, and freeing would race with the agent walking it.
ProgressPoint* find_or_create(const char* name, CounterKind kind) {
  for (;;) {
    ProgressPoint* head = g_points.load(std::memory_order_acquire);
    for (ProgressPoint* p = head; p != nullptr; p = p->next) {
      if (p->kind == kind && p->name == name) return p;
    }

    auto* fresh = new ProgressPoint(name, kind);
    fresh->next = head;
    if (g_points.compare_exchange_weak(head, fresh, std::memory_order_release,
                                       std::memory_order_relaxed)) {
      return fresh;
    }
    // Someone else pushed first: drop ours and rescan, in case they added the
    // very point we wanted.
    delete fresh;
  }
}

// ---------------------------------------------------------------------------
// Agent state (agent thread only, unless noted)
// ---------------------------------------------------------------------------

/// Which stack sampler to drive the experiment loop with.
enum class Sampler {
  /// JVMTI GetAllStackTraces. Specified, stable, works everywhere -- but it
  /// runs at a safepoint, so a sample lands on the last safepoint poll the
  /// thread passed rather than on the instruction it was executing. Attribution
  /// is loop/method-level, not line-level.
  kSafepoint,
  /// AsyncGetCallTrace, walked from a signal handler on the sampled thread
  /// itself. No safepoint, so no bias -- but ASGCT is an undocumented HotSpot
  /// export that appears in no specification and is absent from non-HotSpot VMs.
  kAsgct,
};

struct Options {
  std::string output = "profile.jsonl";
  std::string scope;  // package prefix, in internal form ("com/example")
  int64_t sample_period = 0;  // 0 => per-sampler default
  Sampler sampler = Sampler::kSafepoint;
  bool verbose = false;
};

/// ASGCT walks the stack in a signal handler, so it is cheap enough to sample
/// as often as libcoz does. GetAllStackTraces drags the VM to a safepoint.
constexpr int64_t kAsgctSamplePeriod = 1'000'000;

Options g_options;
jvmtiEnv* g_jvmti = nullptr;
JavaVM* g_vm = nullptr;

std::atomic<bool> g_running{false};

/// The agent's own sampling thread, which must never suspend itself.
jthread g_agent_thread = nullptr;

/// Total samples seen per source line, for the `samples` records.
std::map<std::string, int64_t> g_samples;

/// Diagnostics for --verbose: how many times we sampled, and how long the VM
/// took to reach a safepoint for it.
int64_t g_ticks = 0;
int64_t g_sample_ns = 0;

// Why method_info() gives up (agent thread only).
int64_t g_mi_declaring_fail = 0, g_mi_sig_fail = 0, g_mi_out_of_scope = 0,
        g_mi_file_fail = 0, g_mi_line_fail = 0, g_mi_ok = 0;
jvmtiError g_mi_last_declaring_err = JVMTI_ERROR_NONE;
jvmtiError g_mi_last_line_err = JVMTI_ERROR_NONE;

int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void sleep_ns(int64_t ns) {
  if (ns > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

template <typename T>
void deallocate(T* p) {
  if (p != nullptr) g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(p));
}

// ---------------------------------------------------------------------------
// Line resolution
// ---------------------------------------------------------------------------

/// Cache of jmethodID -> (source path, line table). Agent thread only.
struct MethodInfo {
  bool in_scope = false;
  std::string source;  // "com/example/Toy.java"
  std::vector<jvmtiLineNumberEntry> lines;
};

std::map<jmethodID, MethodInfo> g_methods;

/// Turn "Lcom/example/Toy;" plus "Toy.java" into "com/example/Toy.java", which
/// is what a `coz plot` reader expects a source location to look like.
std::string source_path(const std::string& class_signature, const char* file) {
  std::string internal = class_signature;
  if (internal.size() > 2 && internal.front() == 'L' && internal.back() == ';') {
    internal = internal.substr(1, internal.size() - 2);
  }
  // Nested classes (Outer$Inner) share the outer class's source file.
  const size_t slash = internal.rfind('/');
  const std::string package = (slash == std::string::npos) ? "" : internal.substr(0, slash + 1);
  return package + (file != nullptr ? file : "Unknown");
}

/// True if a class is application code rather than the runtime or a profiler.
bool in_scope(const std::string& class_signature) {
  const std::string internal =
      (class_signature.size() > 2 && class_signature.front() == 'L')
          ? class_signature.substr(1, class_signature.size() - 2)
          : class_signature;

  if (!g_options.scope.empty()) {
    return internal.compare(0, g_options.scope.size(), g_options.scope) == 0;
  }

  // No explicit scope: everything except the runtime and coz itself.
  static const char* const kExcluded[] = {"java/", "javax/", "jdk/",  "sun/",
                                          "com/sun/", "kotlin/", "scala/", "coz/"};
  for (const char* prefix : kExcluded) {
    if (internal.compare(0, strlen(prefix), prefix) == 0) return false;
  }
  return true;
}

/// Look up (and memoize) what we know about a method.
///
/// Returns nullptr when the answer is not knowable *yet* -- the class is not
/// prepared, the VM is in the wrong phase, the method is being unloaded. Those
/// failures must not be memoized: with the ASGCT sampler the first sample of a
/// hot method can arrive a millisecond after VM init, long before its class is
/// prepared, and caching "out of scope" there would silently drop every later
/// sample of that method for the life of the process. (It did: 17k good ASGCT
/// samples resolved to two lines.)
///
/// Only permanent facts are cached: the class is out of scope, or it was
/// compiled without the debug information we need.
const MethodInfo* method_info(jmethodID method) {
  auto it = g_methods.find(method);
  if (it != g_methods.end()) return &it->second;

  jclass declaring = nullptr;
  const jvmtiError decl_err = g_jvmti->GetMethodDeclaringClass(method, &declaring);
  if (decl_err != JVMTI_ERROR_NONE) {
    g_mi_declaring_fail++;
    g_mi_last_declaring_err = decl_err;
    return nullptr;  // transient
  }

  char* signature = nullptr;
  if (g_jvmti->GetClassSignature(declaring, &signature, nullptr) != JVMTI_ERROR_NONE ||
      signature == nullptr) {
    g_mi_sig_fail++;
    return nullptr;  // transient
  }

  MethodInfo info;
  const bool scoped = in_scope(signature);

  if (!scoped) {
    g_mi_out_of_scope++;
    deallocate(signature);
    g_methods[method] = info;  // permanent: wrong package
    return &g_methods[method];
  }

  char* file = nullptr;
  const jvmtiError file_err = g_jvmti->GetSourceFileName(declaring, &file);
  if (file_err == JVMTI_ERROR_ABSENT_INFORMATION) {
    deallocate(signature);
    g_methods[method] = info;  // permanent: compiled without -g:source
    return &g_methods[method];
  }
  if (file_err != JVMTI_ERROR_NONE || file == nullptr) {
    g_mi_file_fail++;
    deallocate(signature);
    return nullptr;  // transient
  }

  jint count = 0;
  jvmtiLineNumberEntry* table = nullptr;
  const jvmtiError line_err = g_jvmti->GetLineNumberTable(method, &count, &table);

  if (line_err == JVMTI_ERROR_ABSENT_INFORMATION || line_err == JVMTI_ERROR_NATIVE_METHOD) {
    deallocate(file);
    deallocate(signature);
    g_methods[method] = info;  // permanent: no line table, or native
    return &g_methods[method];
  }
  if (line_err != JVMTI_ERROR_NONE) {
    g_mi_line_fail++;
    g_mi_last_line_err = line_err;
    deallocate(file);
    deallocate(signature);
    return nullptr;  // transient: class not prepared yet, wrong phase, ...
  }

  g_mi_ok++;
  info.in_scope = true;
  info.source = source_path(signature, file);
  info.lines.assign(table, table + count);

  deallocate(table);
  deallocate(file);
  deallocate(signature);

  g_methods[method] = std::move(info);
  return &g_methods[method];
}

/// Resolve a (method, bytecode index) to "com/example/Toy.java:42", or "" if
/// the method is out of scope or carries no line information.
std::string resolve_line(jmethodID method, jlocation location) {
  const MethodInfo* info = method_info(method);
  if (info == nullptr || !info->in_scope || info->lines.empty()) return "";

  // The line table maps a starting bytecode index to a line; the applicable
  // entry is the last one at or before this location.
  jint line = info->lines.front().line_number;
  for (const auto& entry : info->lines) {
    if (entry.start_location <= location) {
      line = entry.line_number;
    } else {
      break;
    }
  }
  return info->source + ":" + std::to_string(line);
}

// ---------------------------------------------------------------------------
// AsyncGetCallTrace
//
// Not declared in any JDK header: it is an internal HotSpot export, resolved by
// name at run time. The shape below is the one HotSpot's forte.cpp has always
// had. Note that despite its name, ASGCT_CallFrame::lineno holds the *bytecode
// index* for a Java frame, and a negative value for anything else.
// ---------------------------------------------------------------------------

typedef struct {
  jint lineno;
  jmethodID method_id;
} ASGCT_CallFrame;

typedef struct {
  JNIEnv* env_id;
  jint num_frames;  // <= 0 is an error ticks_* code
  ASGCT_CallFrame* frames;
} ASGCT_CallTrace;

typedef void (*AsyncGetCallTrace_t)(ASGCT_CallTrace*, jint, void*);

AsyncGetCallTrace_t g_asgct = nullptr;

// Diagnostics for --verbose: why samples are or are not being recorded.
std::atomic<int64_t> g_sig_entered{0};
std::atomic<int64_t> g_sig_no_self{0};
std::atomic<int64_t> g_sig_bad_trace{0};
std::atomic<int64_t> g_sig_neg_bci{0};
std::atomic<int64_t> g_sig_ok{0};
// Histogram of ASGCT's num_frames error codes (ticks_*), indexed by -code.
std::atomic<int64_t> g_asgct_codes[16];
std::atomic<int64_t> g_asgct_crashes{0};

// ---------------------------------------------------------------------------
// Crash protection
//
// AsyncGetCallTrace is not safe. It walks compiled frames without holding any
// lock, so it races the JIT unloading an nmethod and faults. On stock Temurin
// 17 this crashed the VM within a second:
//
//   SIGBUS at PcDescContainer::find_pc_desc_internal(...)
//
// This is why ASGCT is not the default sampler. To use it at all we bracket the
// call with sigsetjmp and catch SIGSEGV/SIGBUS raised inside it, longjmp'ing
// back out and dropping that one sample.
//
// The JVM uses SIGSEGV constantly for implicit null checks, so a fault that did
// not happen inside ASGCT must be handed straight back to the JVM's handler --
// hence the chaining below. Ours is installed after the VM's, so `previous` is
// the VM's.
// ---------------------------------------------------------------------------

thread_local sigjmp_buf t_asgct_jmp;
thread_local volatile bool t_in_asgct = false;

struct sigaction g_prev_segv;
struct sigaction g_prev_bus;

void chain_to_previous(const struct sigaction& previous, int sig, siginfo_t* info, void* ctx) {
  if (previous.sa_flags & SA_SIGINFO) {
    previous.sa_sigaction(sig, info, ctx);
  } else if (previous.sa_handler == SIG_DFL) {
    // Restore the default and re-raise, so the process dies as it would have.
    signal(sig, SIG_DFL);
    raise(sig);
  } else if (previous.sa_handler != SIG_IGN) {
    previous.sa_handler(sig);
  }
}

void asgct_crash_handler(int sig, siginfo_t* info, void* ctx) {
  if (t_in_asgct) {
    t_in_asgct = false;
    g_asgct_crashes.fetch_add(1, std::memory_order_relaxed);
    siglongjmp(t_asgct_jmp, 1);
  }
  chain_to_previous(sig == SIGBUS ? g_prev_bus : g_prev_segv, sig, info, ctx);
}

// ---------------------------------------------------------------------------
// Thread registry
//
// Append-only and lock-free, like the progress-point list and for the same
// reason: the agent suspends Java threads, so it must never wait on a lock one
// of them could be holding. Entries are reused rather than freed, because the
// agent walks the list concurrently with threads starting and ending.
// ---------------------------------------------------------------------------

struct ThreadEntry {
  std::atomic<bool> alive{false};
  jthread ref = nullptr;  // JNI global ref, stable across the entry's life
  JNIEnv* env = nullptr;
  bool is_agent = false;

  // Written by the signal handler on this thread, read by the agent thread.
  // `seq` is bumped after the fields, and the reader re-reads it, so a torn
  // sample is detected rather than used. `seq` also counts samples, which is
  // how the agent weights a thread that was signalled more than once between
  // drains.
  std::atomic<uint64_t> seq{0};
  jmethodID method = nullptr;
  jlocation bci = 0;

  uint64_t drained = 0;  // agent thread only
  ThreadEntry* next = nullptr;
};

std::atomic<ThreadEntry*> g_threads{nullptr};

/// This thread's registry entry, so the signal handler needs no lookup.
thread_local ThreadEntry* t_self = nullptr;

#ifndef __APPLE__
thread_local timer_t t_timer;
thread_local bool t_has_timer = false;
#endif

ThreadEntry* register_thread(JNIEnv* jni, jthread thread, bool is_agent) {
  // Reuse a dead entry if there is one: a program that churns threads would
  // otherwise grow this list without bound.
  for (ThreadEntry* e = g_threads.load(std::memory_order_acquire); e != nullptr; e = e->next) {
    bool dead = false;
    if (e->alive.compare_exchange_strong(dead, true, std::memory_order_acq_rel)) {
      if (e->ref != nullptr) jni->DeleteGlobalRef(e->ref);
      e->ref = jni->NewGlobalRef(thread);
      e->env = jni;
      e->is_agent = is_agent;
      e->method = nullptr;
      e->bci = 0;
      e->seq.store(0, std::memory_order_release);
      e->drained = 0;
      return e;
    }
  }

  auto* fresh = new ThreadEntry();
  fresh->alive.store(true, std::memory_order_relaxed);
  fresh->ref = jni->NewGlobalRef(thread);
  fresh->env = jni;
  fresh->is_agent = is_agent;

  ThreadEntry* head = g_threads.load(std::memory_order_acquire);
  do {
    fresh->next = head;
  } while (!g_threads.compare_exchange_weak(head, fresh, std::memory_order_release,
                                            std::memory_order_relaxed));
  return fresh;
}

// ---------------------------------------------------------------------------
// The sampling signal handler
// ---------------------------------------------------------------------------

void asgct_signal_handler(int, siginfo_t*, void* ucontext) {
  g_sig_entered.fetch_add(1, std::memory_order_relaxed);
  ThreadEntry* self = t_self;
  if (self == nullptr || self->env == nullptr || g_asgct == nullptr) {
    g_sig_no_self.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  ASGCT_CallFrame frame;
  ASGCT_CallTrace trace;
  trace.env_id = self->env;
  trace.frames = &frame;
  trace.num_frames = 0;

  // sigsetjmp saves the signal mask so a longjmp out of the nested SIGSEGV or
  // SIGBUS handler leaves that signal unblocked again.
  if (sigsetjmp(t_asgct_jmp, 1) != 0) {
    return;  // ASGCT faulted; drop this sample
  }
  t_in_asgct = true;
  g_asgct(&trace, 1, ucontext);
  t_in_asgct = false;

  // num_frames <= 0 is one of ASGCT's ticks_* codes (in native code, at a
  // safepoint, unknown state). A negative lineno means the top frame is not a
  // Java frame. Neither is a sample of Java code.
  if (trace.num_frames < 1) {
    g_sig_bad_trace.fetch_add(1, std::memory_order_relaxed);
    const int code = -trace.num_frames;
    if (code >= 0 && code < 16) g_asgct_codes[code].fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (frame.lineno < 0) {
    g_sig_neg_bci.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  g_sig_ok.fetch_add(1, std::memory_order_relaxed);

  self->method = frame.method_id;
  self->bci = frame.lineno;
  self->seq.fetch_add(1, std::memory_order_release);
}

/// Arm a sampling timer for the calling thread.
///
/// Linux gets a per-thread CLOCK_THREAD_CPUTIME_ID timer, so a thread is
/// sampled in proportion to the CPU time it actually burns, and no thread ever
/// signals another (which would race with that thread exiting).
///
/// macOS has no timer_create. ITIMER_PROF is process-wide and the kernel picks
/// a runnable thread to deliver to, which is still CPU-time-weighted across
/// threads in aggregate -- it just cannot be armed per thread, so it is set up
/// once at VM init rather than here.
void arm_thread_timer() {
#ifndef __APPLE__
  struct sigevent sev;
  memset(&sev, 0, sizeof(sev));
  sev.sigev_notify = SIGEV_THREAD_ID;
  sev.sigev_signo = SIGPROF;
  sev._sigev_un._tid = static_cast<pid_t>(syscall(SYS_gettid));

  if (timer_create(CLOCK_THREAD_CPUTIME_ID, &sev, &t_timer) != 0) return;
  t_has_timer = true;

  struct itimerspec its;
  memset(&its, 0, sizeof(its));
  its.it_interval.tv_nsec = g_options.sample_period % 1000000000;
  its.it_interval.tv_sec = g_options.sample_period / 1000000000;
  its.it_value = its.it_interval;
  timer_settime(t_timer, 0, &its, nullptr);
#endif
}

void disarm_thread_timer() {
#ifndef __APPLE__
  if (t_has_timer) {
    timer_delete(t_timer);
    t_has_timer = false;
  }
#endif
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

struct Sample {
  jthread thread;
  std::string line;  // "" when out of scope
  int64_t weight;    // how many samples this stands for
};

/// One safepoint sample of every Java thread's top frame.
std::vector<Sample> take_safepoint_samples() {
  std::vector<Sample> samples;
  jint thread_count = 0;
  jvmtiStackInfo* stack_info = nullptr;

  const int64_t sample_start = now_ns();
  if (g_jvmti->GetAllStackTraces(1, &stack_info, &thread_count) != JVMTI_ERROR_NONE) {
    return samples;
  }
  g_ticks++;
  g_sample_ns += now_ns() - sample_start;

  JNIEnv* jni = nullptr;
  g_vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6);

  for (jint i = 0; i < thread_count; i++) {
    const jvmtiStackInfo& info = stack_info[i];

    // Never sample or suspend ourselves.
    if (g_agent_thread != nullptr && jni != nullptr &&
        jni->IsSameObject(info.thread, g_agent_thread)) {
      continue;
    }
    if (info.frame_count < 1) continue;

    // The jthread is a JNI local ref; freeing the jvmtiStackInfo array does not
    // invalidate it, so it outlives the Deallocate below.
    samples.push_back({info.thread,
                       resolve_line(info.frame_buffer[0].method, info.frame_buffer[0].location),
                       1});
  }

  deallocate(stack_info);
  return samples;
}

/// Collect whatever the signal handlers have recorded since the last drain.
std::vector<Sample> drain_asgct_samples() {
  std::vector<Sample> samples;

  for (ThreadEntry* e = g_threads.load(std::memory_order_acquire); e != nullptr; e = e->next) {
    if (!e->alive.load(std::memory_order_acquire) || e->is_agent) continue;

    const uint64_t before = e->seq.load(std::memory_order_acquire);
    if (before == e->drained) continue;  // nothing new

    const jmethodID method = e->method;
    const jlocation bci = e->bci;

    // If the handler ran again while we were reading, the pair may be torn.
    // Skip it: the next drain picks up a clean one a millisecond later.
    if (e->seq.load(std::memory_order_acquire) != before) continue;

    const int64_t weight = static_cast<int64_t>(before - e->drained);
    e->drained = before;
    g_ticks += weight;

    static int dumped = 0;
    if (g_options.verbose && dumped < 5) {
      dumped++;
      char* name = nullptr;
      char* sig = nullptr;
      const jvmtiError name_err = g_jvmti->GetMethodName(method, &name, &sig, nullptr);
      fprintf(stderr, "[coz-java]   raw sample: method=%p bci=%lld GetMethodName->%d %s\n",
              (void*)method, (long long)bci, (int)name_err,
              (name_err == JVMTI_ERROR_NONE && name) ? name : "(n/a)");
      if (name) deallocate(name);
      if (sig) deallocate(sig);
    }

    samples.push_back({e->ref, resolve_line(method, bci), weight});
  }

  return samples;
}

std::vector<Sample> take_samples() {
  return g_options.sampler == Sampler::kAsgct ? drain_asgct_samples() : take_safepoint_samples();
}

/// Suspend every thread that is not running `selected`, sleep for `delay_ns`,
/// then resume them. This is the virtual speedup: the selected line appears
/// faster because everything else stood still.
///
/// Returns the delay actually inserted, which the caller subtracts from the
/// experiment's wall time. See the comment on `duration` in profiler_loop().
int64_t insert_delay(const std::vector<Sample>& samples, const std::string& selected,
                     int64_t delay_ns) {
  if (delay_ns <= 0) return 0;

  std::vector<jthread> victims;
  victims.reserve(samples.size());
  for (const Sample& sample : samples) {
    if (sample.line != selected) victims.push_back(sample.thread);
  }
  if (victims.empty()) return 0;

  std::vector<jvmtiError> results(victims.size());
  if (g_jvmti->SuspendThreadList(static_cast<jint>(victims.size()), victims.data(),
                                 results.data()) != JVMTI_ERROR_NONE) {
    return 0;
  }

  const int64_t slept_start = now_ns();
  sleep_ns(delay_ns);
  const int64_t slept = now_ns() - slept_start;

  // Resume only what actually suspended: a thread that died or was already
  // suspended must not be resumed here.
  std::vector<jthread> to_resume;
  to_resume.reserve(victims.size());
  for (size_t i = 0; i < victims.size(); i++) {
    if (results[i] == JVMTI_ERROR_NONE) to_resume.push_back(victims[i]);
  }
  if (!to_resume.empty()) {
    std::vector<jvmtiError> resume_results(to_resume.size());
    g_jvmti->ResumeThreadList(static_cast<jint>(to_resume.size()), to_resume.data(),
                              resume_results.data());
  }

  // The measured pause, not the requested one: sleep overshoots, and that
  // overshoot is real time the program stood still.
  return slept;
}

// ---------------------------------------------------------------------------
// Experiments
// ---------------------------------------------------------------------------

struct Snapshot {
  std::vector<std::pair<ProgressPoint*, int64_t>> counts;
};

Snapshot snapshot_points() {
  Snapshot snap;
  for (ProgressPoint* p = g_points.load(std::memory_order_acquire); p != nullptr; p = p->next) {
    snap.counts.emplace_back(p, p->count.load(std::memory_order_relaxed));
  }
  return snap;
}

bool has_progress_point() { return g_points.load(std::memory_order_acquire) != nullptr; }

/// Choose a line to speed up, weighted by how often it was sampled -- the same
/// bias libcoz gets for free by selecting the line under the sampled PC.
std::string select_line(const std::map<std::string, int64_t>& recent, std::mt19937_64& rng) {
  int64_t total = 0;
  for (const auto& entry : recent) total += entry.second;
  if (total == 0) return "";

  std::uniform_int_distribution<int64_t> pick(0, total - 1);
  int64_t target = pick(rng);
  for (const auto& entry : recent) {
    if (target < entry.second) return entry.first;
    target -= entry.second;
  }
  return recent.begin()->first;
}

void write_json_escaped(std::ostream& out, const std::string& value) {
  for (char c : value) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default: out << c; break;
    }
  }
}

/// The profiler loop: warm up, then run experiments until the VM dies.
void profiler_loop() {
  std::ofstream out(g_options.output, std::ios::app);
  if (!out) {
    fprintf(stderr, "[coz-java] cannot open %s\n", g_options.output.c_str());
    return;
  }
  out.setf(std::ios::fixed, std::ios::floatfield);
  out.precision(2);

  const int64_t start_time = now_ns();
  out << "{\"type\":\"startup\",\"time\":" << start_time << "}\n" << std::flush;

  std::mt19937_64 rng(static_cast<uint64_t>(start_time));
  std::uniform_int_distribution<int> speedup_pick(0, kZeroSpeedupWeight + kSpeedupDivisions);

  // Take one sample of every thread, folding it into `recent` (the pool the
  // next line is drawn from) and into the lifetime totals.
  //
  // Every path that idles must call this. Otherwise `recent` -- which is
  // cleared once a line has been drawn from it -- never refills, select_line()
  // returns nothing forever, and the profiler quietly stops sampling.
  auto collect = [&](std::map<std::string, int64_t>& recent) {
    for (const Sample& sample : take_samples()) {
      if (!sample.line.empty()) {
        recent[sample.line] += sample.weight;
        g_samples[sample.line] += sample.weight;
      }
    }
  };

  // Idle for `duration`, sampling all the while.
  auto idle = [&](std::map<std::string, int64_t>& recent, int64_t duration) {
    const int64_t deadline = now_ns() + duration;
    while (g_running.load(std::memory_order_relaxed) && now_ns() < deadline) {
      collect(recent);
      sleep_ns(g_options.sample_period);
    }
  };

  // Wait for the program to register a progress point and for us to see a line
  // worth speeding up.
  std::map<std::string, int64_t> recent;
  while (g_running.load(std::memory_order_relaxed) && (!has_progress_point() || recent.empty())) {
    collect(recent);
    sleep_ns(g_options.sample_period);
  }

  if (g_options.verbose && g_running.load(std::memory_order_relaxed)) {
    fprintf(stderr, "[coz-java] warmed up: %zu lines, starting experiments\n", recent.size());
  }

  while (g_running.load(std::memory_order_relaxed)) {
    const std::string selected = select_line(recent, rng);
    recent.clear();
    if (selected.empty()) {
      // Nothing in scope has been sampled yet; keep looking.
      collect(recent);
      sleep_ns(g_options.sample_period);
      continue;
    }

    // 0% is drawn with weight kZeroSpeedupWeight so the baseline is measured
    // about half the time; the rest split the 5% increments.
    const int draw = speedup_pick(rng);
    const int division = (draw <= kZeroSpeedupWeight) ? 0 : draw - kZeroSpeedupWeight;
    const double speedup = static_cast<double>(division) / kSpeedupDivisions;
    const int64_t delay_per_sample =
        static_cast<int64_t>(speedup * static_cast<double>(g_options.sample_period));

    const Snapshot before = snapshot_points();
    const int64_t experiment_start = now_ns();

    int64_t selected_samples = 0;
    int64_t batch_delay = 0;
    int64_t inserted_delay = 0;
    int batch = 0;

    for (;;) {
      if (!g_running.load(std::memory_order_relaxed)) break;

      std::vector<Sample> samples = take_samples();

      int64_t on_selected = 0;
      for (const Sample& sample : samples) {
        if (sample.line.empty()) continue;
        recent[sample.line] += sample.weight;
        g_samples[sample.line] += sample.weight;
        if (sample.line == selected) on_selected += sample.weight;
      }
      selected_samples += on_selected;
      batch_delay += on_selected * delay_per_sample;

      // Pay the accumulated debt once per batch, so the VM reaches a safepoint
      // for delay insertion 10x less often than it does for sampling.
      if (++batch >= kSampleBatchSize) {
        inserted_delay += insert_delay(samples, selected, batch_delay);
        batch_delay = 0;
        batch = 0;
      }

      const int64_t elapsed = now_ns() - experiment_start;
      if (elapsed >= kExperimentMinTime) {
        // Keep going until every progress point has moved enough to make the
        // throughput estimate meaningful -- but never past kExperimentMaxTime.
        int64_t min_delta = INT64_MAX;
        for (const auto& entry : before.counts) {
          const int64_t delta = entry.first->count.load(std::memory_order_relaxed) - entry.second;
          if (delta < min_delta) min_delta = delta;
        }
        if (min_delta >= kExperimentTargetDelta || elapsed >= kExperimentMaxTime) break;
      }

      sleep_ns(g_options.sample_period);
    }

    // The virtual speedup is measured against the program's *effective* runtime,
    // not its wall time. Pausing the other threads makes the run longer in real
    // seconds while making the selected line relatively faster; the time we
    // spent paused is not time the program was making progress, so it comes
    // back out. libcoz does the same at profiler.cpp:362
    //   duration = elapsed - experiment_delay
    // Without this, period = duration/delta grows with the speedup and every
    // line reports a negative slope.
    const int64_t duration = (now_ns() - experiment_start) - inserted_delay;

    // Only emit an experiment whose progress points actually advanced; a
    // starved experiment would otherwise inflate the baseline period.
    int64_t min_delta = INT64_MAX;
    for (const auto& entry : before.counts) {
      const int64_t delta = entry.first->count.load(std::memory_order_relaxed) - entry.second;
      if (delta < min_delta) min_delta = delta;
    }
    if (min_delta == INT64_MAX || min_delta < kExperimentTargetDelta) {
      // A starved experiment tells us nothing, and emitting it would drag the
      // aggregated baseline period around. Drop it, but keep sampling so the
      // next round has a line to pick.
      idle(recent, kExperimentCoolOff);
      continue;
    }

    out << "{\"type\":\"experiment\",\"selected\":\"";
    write_json_escaped(out, selected);
    out << "\",\"speedup\":" << speedup << ",\"duration\":" << duration
        << ",\"selected_samples\":" << selected_samples << "}\n";

    // Each experiment record is immediately followed by the delta for every
    // progress point, which is the format `coz plot` expects.
    for (const auto& entry : before.counts) {
      ProgressPoint* point = entry.first;
      const int64_t delta = point->count.load(std::memory_order_relaxed) - entry.second;
      const char* kind = (point->kind == kThroughput) ? "throughput-point" : "latency-point";
      out << "{\"type\":\"" << kind << "\",\"name\":\"";
      write_json_escaped(out, point->name);
      out << "\",\"delta\":" << delta << "}\n";
    }
    out << std::flush;

    idle(recent, kExperimentCoolOff);
  }

  for (const auto& entry : g_samples) {
    out << "{\"type\":\"samples\",\"location\":\"";
    write_json_escaped(out, entry.first);
    out << "\",\"count\":" << entry.second << "}\n";
  }
  out << "{\"type\":\"runtime\",\"time\":" << (now_ns() - start_time) << "}\n";
  out.flush();

  if (g_options.verbose) {
    if (g_options.sampler == Sampler::kAsgct) {
      fprintf(stderr,
              "[coz-java] wrote %s (%lld ASGCT samples; sigprof=%lld no_self=%lld "
              "bad_trace=%lld neg_bci=%lld ok=%lld)\n",
              g_options.output.c_str(), static_cast<long long>(g_ticks),
              (long long)g_sig_entered.load(), (long long)g_sig_no_self.load(),
              (long long)g_sig_bad_trace.load(), (long long)g_sig_neg_bci.load(),
              (long long)g_sig_ok.load());
      fprintf(stderr, "[coz-java]   ASGCT faults caught: %lld\n",
              (long long)g_asgct_crashes.load());
      fprintf(stderr,
              "[coz-java]   method_info: ok=%lld declaring_fail=%lld(err=%d) sig_fail=%lld "
              "out_of_scope=%lld file_fail=%lld line_fail=%lld(err=%d)\n",
              (long long)g_mi_ok, (long long)g_mi_declaring_fail, (int)g_mi_last_declaring_err,
              (long long)g_mi_sig_fail, (long long)g_mi_out_of_scope, (long long)g_mi_file_fail,
              (long long)g_mi_line_fail, (int)g_mi_last_line_err);
      static const char* kTicks[] = {
          "no_Java_frame", "no_class_load", "GC_active", "unknown_not_Java",
          "not_walkable_not_Java", "unknown_Java", "not_walkable_Java",
          "unknown_state", "thread_exit", "deopt", "safepoint"};
      for (int i = 0; i < 16; i++) {
        const int64_t n = g_asgct_codes[i].load();
        if (n > 0) {
          fprintf(stderr, "[coz-java]   ticks[-%d] %-22s %lld\n", i,
                  i < 11 ? kTicks[i] : "?", (long long)n);
        }
      }
    } else {
      fprintf(stderr,
              "[coz-java] wrote %s (%lld sample ticks, mean GetAllStackTraces %lld us)\n",
              g_options.output.c_str(), static_cast<long long>(g_ticks),
              static_cast<long long>(g_ticks ? g_sample_ns / g_ticks / 1000 : 0));
    }
  }
}

void JNICALL sampler_entry(jvmtiEnv*, JNIEnv*, void*) { profiler_loop(); }

// ---------------------------------------------------------------------------
// Natives behind coz.Coz
// ---------------------------------------------------------------------------

jboolean JNICALL Coz_available(JNIEnv*, jclass) { return JNI_TRUE; }

void bump(JNIEnv* jni, jstring name, CounterKind kind) {
  if (name == nullptr) return;
  const char* chars = jni->GetStringUTFChars(name, nullptr);
  if (chars == nullptr) return;
  find_or_create(chars, kind)->count.fetch_add(1, std::memory_order_relaxed);
  jni->ReleaseStringUTFChars(name, chars);
}

void JNICALL Coz_progress(JNIEnv* jni, jclass, jstring name) { bump(jni, name, kThroughput); }
void JNICALL Coz_begin(JNIEnv* jni, jclass, jstring name) { bump(jni, name, kBegin); }
void JNICALL Coz_end(JNIEnv* jni, jclass, jstring name) { bump(jni, name, kEnd); }

// ---------------------------------------------------------------------------
// Agent lifecycle
// ---------------------------------------------------------------------------

void parse_options(const char* options) {
  if (options == nullptr) return;

  std::string rest(options);
  while (!rest.empty()) {
    const size_t comma = rest.find(',');
    std::string item = rest.substr(0, comma);
    rest = (comma == std::string::npos) ? "" : rest.substr(comma + 1);

    const size_t eq = item.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = item.substr(0, eq);
    const std::string value = item.substr(eq + 1);

    if (key == "output") {
      g_options.output = value;
    } else if (key == "scope") {
      g_options.scope = value;
      // Accept dotted package names; JVMTI signatures are slash-separated.
      for (char& c : g_options.scope) {
        if (c == '.') c = '/';
      }
    } else if (key == "period") {
      g_options.sample_period = std::strtoll(value.c_str(), nullptr, 10);
    } else if (key == "sampler") {
      if (value == "asgct") {
        g_options.sampler = Sampler::kAsgct;
      } else if (value == "safepoint") {
        g_options.sampler = Sampler::kSafepoint;
      } else {
        fprintf(stderr, "[coz-java] unknown sampler '%s'; using safepoint\n", value.c_str());
      }
    } else if (key == "verbose") {
      g_options.verbose = (value != "0");
    }
  }
}

jthread alloc_agent_thread(JNIEnv* jni) {
  jclass thread_class = jni->FindClass("java/lang/Thread");
  jmethodID ctor = jni->GetMethodID(thread_class, "<init>", "(Ljava/lang/String;)V");
  jstring name = jni->NewStringUTF("coz-profiler");
  jthread thread = static_cast<jthread>(jni->NewObject(thread_class, ctor, name));
  return static_cast<jthread>(jni->NewGlobalRef(thread));
}

/// Resolve AsyncGetCallTrace and install the sampling signal handler.
/// Returns false if ASGCT is unavailable, in which case the caller falls back.
bool setup_asgct() {
  g_asgct = reinterpret_cast<AsyncGetCallTrace_t>(dlsym(RTLD_DEFAULT, "AsyncGetCallTrace"));
  if (g_asgct == nullptr) {
    fprintf(stderr,
            "[coz-java] AsyncGetCallTrace not found (not a HotSpot JVM?); "
            "falling back to the safepoint sampler\n");
    return false;
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = &asgct_signal_handler;
  // SA_ONSTACK because the JVM gives its threads an alternate signal stack and
  // expects foreign handlers to use it. SA_RESTART so we do not turn every
  // blocking syscall in the program into an EINTR.
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
  sigemptyset(&sa.sa_mask);

  struct sigaction previous;
  if (sigaction(SIGPROF, &sa, &previous) != 0) {
    fprintf(stderr, "[coz-java] could not install the SIGPROF handler\n");
    g_asgct = nullptr;
    return false;
  }
  if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN) {
    fprintf(stderr,
            "[coz-java] warning: replaced an existing SIGPROF handler; another "
            "profiler in this process will stop working\n");
  }

  // Install crash protection *after* the VM's own handlers, so the saved
  // `previous` is the VM's and we can hand non-ASGCT faults straight back.
  struct sigaction guard;
  memset(&guard, 0, sizeof(guard));
  guard.sa_sigaction = &asgct_crash_handler;
  guard.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
  sigemptyset(&guard.sa_mask);
  if (sigaction(SIGSEGV, &guard, &g_prev_segv) != 0 ||
      sigaction(SIGBUS, &guard, &g_prev_bus) != 0) {
    fprintf(stderr, "[coz-java] could not install ASGCT crash protection\n");
    g_asgct = nullptr;
    return false;
  }

#ifdef __APPLE__
  // No timer_create on macOS: one process-wide ITIMER_PROF, delivered by the
  // kernel to a runnable thread.
  struct itimerval timer;
  timer.it_interval.tv_sec = static_cast<time_t>(g_options.sample_period / 1000000000);
  timer.it_interval.tv_usec = static_cast<suseconds_t>((g_options.sample_period % 1000000000) / 1000);
  timer.it_value = timer.it_interval;
  if (setitimer(ITIMER_PROF, &timer, nullptr) != 0) {
    fprintf(stderr, "[coz-java] could not arm ITIMER_PROF\n");
    g_asgct = nullptr;
    return false;
  }
#endif

  return true;
}

/// Runs on the thread that is starting, which is what lets it arm that thread's
/// own timer without ever signalling across threads.
void JNICALL on_thread_start(jvmtiEnv*, JNIEnv* jni, jthread thread) {
  if (g_options.sampler != Sampler::kAsgct) return;

  const bool is_agent =
      (g_agent_thread != nullptr && jni->IsSameObject(thread, g_agent_thread));
  t_self = register_thread(jni, thread, is_agent);

  // The agent thread must not sample itself.
  if (!is_agent) arm_thread_timer();
}

/// Deliberately empty.
///
/// AsyncGetCallTrace refuses to walk a stack unless the VM is posting ClassLoad
/// events: forte.cpp opens with
///
///     if (!JvmtiExport::should_post_class_load()) {
///       trace->num_frames = ticks_no_class_load;   // -1
///       return;
///     }
///
/// Without these callbacks enabled, every single call returns -1 and the
/// sampler collects nothing. Enabling the event is the whole point; the handler
/// itself has no work to do.
void JNICALL on_class_load(jvmtiEnv*, JNIEnv*, jthread, jclass) {}
void JNICALL on_class_prepare(jvmtiEnv*, JNIEnv*, jthread, jclass) {}

void JNICALL on_thread_end(jvmtiEnv*, JNIEnv*, jthread) {
  if (g_options.sampler != Sampler::kAsgct) return;

  disarm_thread_timer();
  if (t_self != nullptr) {
    // Stop the handler from touching the entry, and release it for reuse. The
    // global ref stays until a new thread claims the slot, so the agent thread
    // never sees a dangling jthread.
    t_self->alive.store(false, std::memory_order_release);
    t_self = nullptr;
  }
}

void JNICALL on_vm_init(jvmtiEnv* jvmti, JNIEnv* jni, jthread thread) {
  jclass coz = jni->FindClass("coz/Coz");
  if (coz == nullptr) {
    // The application never loaded coz.Coz. Sampling would still work, but
    // with no progress point there is nothing to correlate a speedup against.
    jni->ExceptionClear();
    fprintf(stderr,
            "[coz-java] coz.Coz not found on the classpath; no progress points, "
            "no profile will be written\n");
    return;
  }

  static const JNINativeMethod natives[] = {
      {const_cast<char*>("available0"), const_cast<char*>("()Z"),
       reinterpret_cast<void*>(Coz_available)},
      {const_cast<char*>("progress0"), const_cast<char*>("(Ljava/lang/String;)V"),
       reinterpret_cast<void*>(Coz_progress)},
      {const_cast<char*>("begin0"), const_cast<char*>("(Ljava/lang/String;)V"),
       reinterpret_cast<void*>(Coz_begin)},
      {const_cast<char*>("end0"), const_cast<char*>("(Ljava/lang/String;)V"),
       reinterpret_cast<void*>(Coz_end)},
  };

  if (jni->RegisterNatives(coz, natives, 4) != 0) {
    fprintf(stderr, "[coz-java] failed to register natives on coz.Coz\n");
    return;
  }

  g_agent_thread = alloc_agent_thread(jni);

  if (g_options.sampler == Sampler::kAsgct && !setup_asgct()) {
    g_options.sampler = Sampler::kSafepoint;
    g_options.sample_period = kDefaultSamplePeriod;
  }

  if (g_options.sampler == Sampler::kAsgct) {
    // Required, not optional: AsyncGetCallTrace returns ticks_no_class_load for
    // every call unless the VM is posting ClassLoad events. See on_class_load.
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_LOAD, nullptr);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_PREPARE, nullptr);

    // ThreadStart fires only for threads started from here on, so the thread we
    // are on -- the main thread -- has to register and arm itself.
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_THREAD_START, nullptr);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_THREAD_END, nullptr);
    t_self = register_thread(jni, thread, false);
    arm_thread_timer();
  }

  g_running.store(true, std::memory_order_relaxed);
  jvmti->RunAgentThread(g_agent_thread, &sampler_entry, nullptr, JVMTI_THREAD_NORM_PRIORITY);

  if (g_options.verbose) {
    fprintf(stderr, "[coz-java] agent started (output=%s, scope=%s, period=%lldns)\n",
            g_options.output.c_str(), g_options.scope.empty() ? "<application>" : g_options.scope.c_str(),
            static_cast<long long>(g_options.sample_period));
  }
}

void JNICALL on_vm_death(jvmtiEnv*, JNIEnv*) {
  // Let the profiler loop notice, finish its current experiment, and flush.
  g_running.store(false, std::memory_order_relaxed);
  // The agent thread sleeps at most one sample period between checks, plus the
  // time to write its summary records.
  sleep_ns(g_options.sample_period * 4);
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void*) {
  g_vm = vm;
  parse_options(options);

  if (g_options.sample_period == 0) {
    g_options.sample_period =
        (g_options.sampler == Sampler::kAsgct) ? kAsgctSamplePeriod : kDefaultSamplePeriod;
  }

  if (vm->GetEnv(reinterpret_cast<void**>(&g_jvmti), JVMTI_VERSION_1_2) != JNI_OK) {
    fprintf(stderr, "[coz-java] this JVM does not support JVMTI 1.2\n");
    return JNI_ERR;
  }

  jvmtiCapabilities caps;
  memset(&caps, 0, sizeof(caps));
  caps.can_suspend = 1;                 // insert virtual delays
  caps.can_get_line_numbers = 1;        // map bytecode index to source line
  caps.can_get_source_file_name = 1;    // name the source file
  if (g_jvmti->AddCapabilities(&caps) != JVMTI_ERROR_NONE) {
    fprintf(stderr, "[coz-java] JVM denied the capabilities coz needs\n");
    return JNI_ERR;
  }

  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.VMInit = &on_vm_init;
  callbacks.VMDeath = &on_vm_death;
  callbacks.ThreadStart = &on_thread_start;
  callbacks.ThreadEnd = &on_thread_end;
  callbacks.ClassLoad = &on_class_load;
  callbacks.ClassPrepare = &on_class_prepare;
  if (g_jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks)) != JVMTI_ERROR_NONE) {
    return JNI_ERR;
  }

  g_jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, nullptr);
  g_jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, nullptr);
  return JNI_OK;
}

extern "C" JNIEXPORT void JNICALL Agent_OnUnload(JavaVM*) {
  g_running.store(false, std::memory_order_relaxed);
}
