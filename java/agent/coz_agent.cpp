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

struct Options {
  std::string output = "profile.jsonl";
  std::string scope;  // package prefix, in internal form ("com/example")
  int64_t sample_period = kDefaultSamplePeriod;
  bool verbose = false;
};

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

const MethodInfo* method_info(jmethodID method) {
  auto it = g_methods.find(method);
  if (it != g_methods.end()) return &it->second;

  MethodInfo info;

  jclass declaring = nullptr;
  if (g_jvmti->GetMethodDeclaringClass(method, &declaring) != JVMTI_ERROR_NONE) {
    g_methods[method] = info;
    return &g_methods[method];
  }

  char* signature = nullptr;
  if (g_jvmti->GetClassSignature(declaring, &signature, nullptr) == JVMTI_ERROR_NONE &&
      signature != nullptr) {
    if (in_scope(signature)) {
      char* file = nullptr;
      // Classes compiled without -g:source have no source file name; such a
      // method can never be attributed to a line, so it stays out of scope.
      if (g_jvmti->GetSourceFileName(declaring, &file) == JVMTI_ERROR_NONE && file != nullptr) {
        jint count = 0;
        jvmtiLineNumberEntry* table = nullptr;
        // Likewise for -g:lines. Native and abstract methods return
        // JVMTI_ERROR_NATIVE_METHOD / ABSENT_INFORMATION here.
        if (g_jvmti->GetLineNumberTable(method, &count, &table) == JVMTI_ERROR_NONE) {
          info.in_scope = true;
          info.source = source_path(signature, file);
          info.lines.assign(table, table + count);
          deallocate(table);
        }
        deallocate(file);
      }
    }
    deallocate(signature);
  }

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
// Sampling
// ---------------------------------------------------------------------------

struct Sample {
  jthread thread;    // local ref, valid for this sampling round
  std::string line;  // "" when out of scope
};

/// Take one sample of every Java thread's top frame.
///
/// `stack_info` is returned so the caller can free it only after it is done
/// with the jthread refs inside.
std::vector<Sample> take_sample(jvmtiStackInfo** stack_info) {
  std::vector<Sample> samples;
  jint thread_count = 0;
  *stack_info = nullptr;

  const int64_t sample_start = now_ns();
  if (g_jvmti->GetAllStackTraces(1, stack_info, &thread_count) != JVMTI_ERROR_NONE) {
    return samples;
  }
  g_ticks++;
  g_sample_ns += now_ns() - sample_start;

  JNIEnv* jni = nullptr;
  g_vm->GetEnv(reinterpret_cast<void**>(&jni), JNI_VERSION_1_6);

  for (jint i = 0; i < thread_count; i++) {
    const jvmtiStackInfo& info = (*stack_info)[i];

    // Never sample or suspend ourselves.
    if (g_agent_thread != nullptr && jni != nullptr &&
        jni->IsSameObject(info.thread, g_agent_thread)) {
      continue;
    }
    if (info.frame_count < 1) continue;

    Sample sample;
    sample.thread = info.thread;
    sample.line = resolve_line(info.frame_buffer[0].method, info.frame_buffer[0].location);
    samples.push_back(std::move(sample));
  }

  return samples;
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
    jvmtiStackInfo* stack_info = nullptr;
    for (const Sample& sample : take_sample(&stack_info)) {
      if (!sample.line.empty()) {
        recent[sample.line]++;
        g_samples[sample.line]++;
      }
    }
    deallocate(stack_info);
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

      jvmtiStackInfo* stack_info = nullptr;
      std::vector<Sample> samples = take_sample(&stack_info);

      int64_t on_selected = 0;
      for (const Sample& sample : samples) {
        if (sample.line.empty()) continue;
        recent[sample.line]++;
        g_samples[sample.line]++;
        if (sample.line == selected) on_selected++;
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

      deallocate(stack_info);

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
    fprintf(stderr,
            "[coz-java] wrote %s (%lld sample ticks, mean GetAllStackTraces %lld us)\n",
            g_options.output.c_str(), static_cast<long long>(g_ticks),
            static_cast<long long>(g_ticks ? g_sample_ns / g_ticks / 1000 : 0));
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

void JNICALL on_vm_init(jvmtiEnv* jvmti, JNIEnv* jni, jthread) {
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
