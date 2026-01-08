# macOS Port TODO

## Current Status: Fully Working (DWARF 4 & 5)

The macOS port is **fully functional** with the following capabilities:
- Thread suspension sampling using Mach APIs
- Sample capture and matching to source lines
- Experiment execution with virtual speedups
- Profile output generation
- **DWARF 5 support** (including indexed addresses and strings)

## Completed Work

### Thread Suspension Sampling (Implemented)
Replaced signal-based sampling with Mach thread suspension:

```cpp
// Sample ALL threads in the task (not just registered ones)
thread_act_array_t threads;
mach_msg_type_number_t thread_count;
task_threads(mach_task_self(), &threads, &thread_count);

for (each thread) {
    thread_suspend(thread);

    // Get PC using arm_thread_state64_get_pc() for PAC compatibility
    arm_thread_state64_t state;
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    thread_get_state(thread, ARM_THREAD_STATE64, (thread_state_t)&state, &count);
    uint64_t ip = arm_thread_state64_get_pc(state);  // Strips PAC bits

    thread_resume(thread);
}
```

**Key insight**: Using `task_threads()` to sample ALL threads in the process, not just registered ones. This is necessary because pthread interposition doesn't work reliably on macOS with `DYLD_INSERT_LIBRARIES`.

### Main Executable Path Detection (Fixed)
Fixed `_dyld_get_image_name(0)` returning libcoz.so instead of the main executable when using `DYLD_INSERT_LIBRARIES`:

```cpp
#ifdef __APPLE__
char path[PATH_MAX];
uint32_t size = sizeof(path);
if (_NSGetExecutablePath(path, &size) == 0) {
    char real_path[PATH_MAX];
    realpath(path, real_path);
    // Use real_path as main executable
}
#endif
```

### Mach-O DWARF Section Name Truncation (Fixed)
Mach-O section names are limited to 16 characters, causing `__debug_str_offsets` to be truncated to `__debug_str_offs`. Added handling in `macho_support.cpp`:

```cpp
else if(std::strcmp(suffix, "str_offsets") == 0 ||
        std::strcmp(suffix, "str_offs") == 0)
    *out = dwarf::section_type::str_offsets;
```

### dSYM Bundle Support (Implemented)
- Automatically detects `<binary>.dSYM/Contents/Resources/DWARF/<binary>`
- Falls back to running `dsymutil` to generate dSYM if not found
- Filters DYLD environment variables when spawning dsymutil

### Mach Time Conversion (Fixed)
`mach_absolute_time()` returns Mach time units, NOT nanoseconds. On Apple Silicon, the units are typically 1:1 with nanoseconds, but on Intel Macs they differ. Fixed in `util.h`:

```cpp
static size_t get_time() {
#if defined(__APPLE__)
  static mach_timebase_info_data_t timebase_info = {0, 0};
  if (timebase_info.denom == 0) {
    mach_timebase_info(&timebase_info);
  }
  uint64_t mach_time = mach_absolute_time();
  // Convert to nanoseconds: ns = mach_time * numer / denom
  return (mach_time * timebase_info.numer) / timebase_info.denom;
#else
  // Linux: clock_gettime(CLOCK_MONOTONIC, ...)
#endif
}
```

### Direct Sample Callback for Reliable Counting (Fixed)
Signal-based sample processing was unreliable during experiments - the main thread wouldn't always process samples from the ring buffer promptly. Fixed by adding a direct callback mechanism:

```cpp
// In perf_macos.cpp - sampling thread calls this directly
static std::atomic<void(*)(uint64_t ip)> g_sample_callback{nullptr};

void macos_set_sample_callback(void(*callback)(uint64_t ip)) {
  g_sample_callback.store(callback, std::memory_order_release);
}

// In sample_all_threads():
if (ip != 0) {
  auto callback = g_sample_callback.load(std::memory_order_acquire);
  if (callback) {
    callback(ip);  // Directly increment line sample count
  }
}

// In profiler.cpp - registered at startup:
static void macos_sample_callback(uint64_t ip) {
  std::shared_ptr<line> l = memory_map::get_instance().find_line(ip);
  if (l) {
    l->add_sample();
  }
}
```

This ensures sample counts are updated immediately when samples are collected, not dependent on signal delivery timing.

## Known Limitations

### 1. Wrapper Programs Break Main Executable Detection
When using wrapper programs like `timeout`, the main executable detection picks up the wrapper instead of the target:

```bash
# This fails - detects "timeout" as main executable
timeout 15 ./myapp

# This works
./myapp &
sleep 15
kill $!
```

**Workaround**: Use explicit binary scope:
```bash
COZ_BINARY_SCOPE=/path/to/myapp ./myapp
```

### 2. pthread Interposition Doesn't Work
macOS DYLD interposition via `__DATA,__interpose` section doesn't reliably intercept pthread calls when using `DYLD_INSERT_LIBRARIES`. The workaround (sampling all threads via `task_threads()`) is already implemented.

## Files Modified for macOS Port

| File | Changes |
|------|---------|
| `libcoz/perf_macos.h` | Thread suspension sampling header, sample buffer, callback declaration |
| `libcoz/perf_macos.cpp` | Mach thread suspension sampling, direct sample callback mechanism |
| `libcoz/ccutil/timer.h` | macOS timer using dispatch sources |
| `libcoz/util.h` | `mach_absolute_time()` to nanoseconds conversion |
| `libcoz/profiler.cpp` | macOS-specific sample processing, sample callback registration |
| `libcoz/inspect.cpp` | macOS `get_loaded_files()` using dyld APIs, exception handling for DWARF |
| `libcoz/libcoz.cpp` | `_NSGetExecutablePath()` for main executable detection |
| `libcoz/macho_support.cpp` | Mach-O DWARF loading, dSYM bundle support, dsymutil integration, DWARF 5 sections |
| `libcoz/macho_support.h` | Mach-O support header |
| `libcoz/mac_interpose.cpp` | pthread interposition (not working, kept for reference) |
| `libcoz/real.cpp` | macOS pthread handle using `RTLD_NEXT` |
| `libcoz/elf_compat.h` | ELF compatibility types for macOS |
| `libelfin/dwarf/dwarf++.hh` | Added DWARF 5 section types (addr, loclists, rnglists) |
| `libelfin/dwarf/dwarf.cc` | Fixed section format/address detection for DWARF 5 |
| `libelfin/dwarf/elf.cc` | Added DWARF 5 section name mappings |
| `libelfin/dwarf/value.cc` | Implemented `DW_FORM_addrx*` address resolution |
| `libelfin/dwarf/to_string.cc` | Added DWARF 5 section type strings |

## DWARF 5 Support

DWARF 5 is now fully supported on macOS. The following features were implemented:

### Section Support
- `.debug_addr` - Address table for indexed addresses (`DW_FORM_addrx*`)
- `.debug_loclists` - Location lists (section type registered, full parsing deferred)
- `.debug_rnglists` - Range lists (section type registered, full parsing deferred)

### Form Code Support
- `DW_FORM_addrx`, `addrx1`, `addrx2`, `addrx3`, `addrx4` - Indexed addresses
- `DW_FORM_strx`, `strx1`, `strx2`, `strx3`, `strx4` - Indexed strings (already supported)

### Key Implementation Details

**Address resolution** (`value.cc`):
```cpp
case DW_FORM::addrx:
case DW_FORM::addrx1: // etc.
    // Read address index from form
    // Look up in .debug_addr section at:
    //   header_size + index * addr_size
    auto addr_sec = cu->get_dwarf().get_section(section_type::addr);
    cursor addr_cur(addr_sec, header_size + index * addr_size);
    return addr_cur.address();
```

**Section format detection** (`dwarf.cc`):
- Detect DWARF32/DWARF64 format from first CU header
- Detect address size from CU header (different position in DWARF 5 vs 2-4)
- Propagate format and address size to all loaded sections

## Next Steps

### 1. Improve Wrapper Program Handling
Options:
- Detect wrapper programs and skip them in binary scope
- Add `COZ_SKIP_BINARIES` environment variable
- Walk the process tree to find the actual target

### 2. Consider kperf for Lower Overhead
macOS private kperf framework could provide lower-overhead sampling than thread suspension, but:
- Private API may change without notice
- Requires elevated privileges or SIP adjustments
- Cannot be used in App Store applications

### 3. Test with More Complex Applications
- Multi-process applications
- Applications using Grand Central Dispatch (GCD)
- Applications with many short-lived threads

## Testing

```bash
# Build benchmark (default DWARF 5 on modern macOS)
cd benchmarks/toy
c++ -g -O2 -c toy.cpp -o toy.o -I../../include
c++ -g toy.o -o toy -lpthread
dsymutil toy
rm toy.o

# Or explicitly with DWARF 4 if needed
# c++ -g -gdwarf-4 -O2 -c toy.cpp -o toy.o -I../../include

# Run profiler
cd ../..
DYLD_INSERT_LIBRARIES=./build/libcoz/libcoz.so \
COZ_OUTPUT=/tmp/test.coz \
COZ_BINARY_SCOPE=MAIN \
COZ_SOURCE_SCOPE='%' \
./benchmarks/toy/toy

# Check output
cat /tmp/test.coz
```

Expected output includes:
- `startup` records with timestamps
- `runtime` records
- `samples` records with source locations
- `experiment` records with speedup values
- `throughput-point` records

## References

- [Mach Thread APIs](https://developer.apple.com/documentation/kernel/mach_thread_apis)
- [DWARF 5 Specification](https://dwarfstd.org/dwarf5std.html)
- [libelfin](https://github.com/plasma-umass/libelfin)
- [Coz Paper (SOSP 2015)](https://arxiv.org/abs/1608.03676)
