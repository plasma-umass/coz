# TODO

## macOS Profiling: Speedup Prediction Accuracy

### Status: FIXED (2026-01-12)

The macOS profiler now produces accurate speedup predictions comparable to Linux.

**Results with `toy` benchmark:**
| Metric | Linux | macOS (before) | macOS (after) |
|--------|-------|----------------|---------------|
| Line 18 predicted speedup | +56.7% | +12% | **+54.7%** |
| Line 23 predicted speedup | +4.2% | ~0% | ~0% |

Results verified with both absolute paths (`/full/path/toy-mac-dwarf5`) and relative paths (`./toy-mac-dwarf5`).

### Root Cause (was)

The causal profiling algorithm works by inserting **virtual delays** to simulate speeding up code. On macOS, delays were being added to the wrong thread:

1. A global sampling thread samples ALL threads via Mach APIs
2. All samples go to one ring buffer, processed by the main thread
3. Main thread was adding delays to itself instead of the thread that produced the sample
4. Additionally, per-sample signaling via `pthread_kill()` was overwhelming the main thread

### Solution Implemented

**Option 2: Cross-Thread Delay Distribution** was implemented with a critical signal handling fix:

1. **Added `pending_delay` atomic field** (`thread_state.h`)
   - Each thread has an atomic counter for delays assigned by other threads
   - Allows the processing thread to distribute delays to the correct thread

2. **Modified `process_samples()`** (`profiler.cpp`)
   - On macOS, looks up the originating thread by sample `tid`
   - Adds delay to that thread's `pending_delay` instead of `local_delay`

3. **Modified `add_delays()`** (`profiler.cpp`)
   - On macOS, consumes `pending_delay` at the start of each call
   - Threads pick up their assigned delays via their periodic timer

4. **Fixed signal handling overhead** (`profiler.cpp:begin_sampling()`)
   - Removed `set_ready_signal()` call that was causing per-sample signaling
   - The sampling thread was sending ~1000 signals/sec per thread via `pthread_kill()`
   - This overwhelmed the main thread, preventing program execution
   - Now relies solely on timer-based batching (ITIMER_PROF every 10ms)

### Files Modified

- `libcoz/thread_state.h` - Added `pending_delay` atomic field
- `libcoz/profiler.cpp` - Cross-thread delay distribution + signal fix
- `libcoz/perf_macos.cpp` - macOS sampling implementation (unchanged)
- `libcoz/perf_macos.h` - macOS perf_event class (unchanged)

### Previous Fixes (still in place)

1. **Duration underflow** (`profiler.cpp`)
   - When `experiment_delay > raw_elapsed`, unsigned subtraction underflowed
   - Fixed with clamp to 0

2. **Duration calculation** (`profiler.cpp`)
   - On Mac, don't subtract `experiment_delay` from duration
   - The delay mechanism affects the measuring thread differently than on Linux

3. **Relative path dSYM lookup** (`inspect.cpp`) - Fixed 2026-01-12
   - `get_loaded_files()` was filtering out relative paths (checking `name[0] == '/'`)
   - When running `./binary` instead of `/full/path/binary`, the main binary was excluded
   - No debug info was loaded, causing no speedup predictions
   - Fixed by converting relative paths to absolute via `canonicalize_path()`
   - Now works correctly with both `./toy-mac-dwarf5` and absolute paths

### Known Limitations

- macOS uses `ITIMER_PROF` which is per-process, not per-thread
- Sample counts may be lower than Linux due to different sampling mechanisms
- Some noise in predictions is expected due to the statistical nature of causal profiling

### Outstanding Issue: Thread Interposition

**Status: Under Investigation**

On macOS without `DYLD_FORCE_FLAT_NAMESPACE=1`, pthread interposition does not work for
application code. This affects the causal profiling algorithm:

1. **Problem**: macOS two-level namespace causes binaries to link directly to libpthread,
   bypassing the interposed `pthread_create` in libcoz.
2. **Effect**: Worker threads created by std::thread or pthread_create aren't registered
   with the profiler and don't respond to the delay mechanism.
3. **Result**: Speedup predictions may be inaccurate or show unexpected patterns
   (e.g., curve peaking then returning to 0%).

**Workarounds**:
- Use `DYLD_FORCE_FLAT_NAMESPACE=1` for more accurate results (trades off stability)
- Results are most accurate for single-threaded programs or programs where the
  profiled code runs in the main thread

**Technical Details**:
- DYLD_INTERPOSE tuples are present in libcoz but only work with flat namespace
- Without flat namespace, only calls from within libcoz itself are intercepted
- The `_main_sampler_state` mechanism ensures samples are processed correctly,
  but delay distribution to unregistered threads cannot work
