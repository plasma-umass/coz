//! Rust support for the `coz` Causal Profiler
//!
//! This crate is a translation of the `coz.h` header file provided by `coz` to
//! Rust, and enables profiling Rust programs with `coz` to profile throughput
//! and latency.
//!
//! For usage information, consult the [`README.md` for the `coz`
//! repository][coz-readme] as well as the [`README.md` for
//! `coz-rs`][rust-readme].
//!
//! [coz-readme]: https://github.com/plasma-umass/coz/blob/master/README.md
//! [rust-readme]: https://github.com/alexcrichton/coz-rs/blob/master/README.md

use once_cell::sync::OnceCell;
use std::cell::Cell;
use std::ffi::{CStr, CString};
use std::mem;
use std::ptr;
use std::sync::atomic::{AtomicUsize, Ordering::SeqCst};

/// Equivalent of the `COZ_PROGRESS` and `COZ_PROGRESS_NAMED` macros
///
/// This can be executed as:
///
/// ```
/// coz::progress!();
/// ```
///
/// or ...
///
/// ```
/// coz::progress!("my unique name");
/// ```
#[macro_export]
macro_rules! progress {
    () => {{
        static COUNTER: $crate::Counter =
            $crate::Counter::progress(concat!(file!(), ":", line!()));
        COUNTER.increment();
    }};
    ($name:expr) => {{
        static COUNTER: $crate::Counter = $crate::Counter::progress($name);
        COUNTER.increment();
    }};
}

/// Equivalent of the `COZ_BEGIN` macro
///
/// This can be executed as:
///
/// ```
/// coz::begin!("foo");
/// ```
#[macro_export]
macro_rules! begin {
    ($name:expr) => {{
        static COUNTER: $crate::Counter = $crate::Counter::begin($name);
        COUNTER.increment();
    }};
}

/// Equivalent of the `COZ_END` macro
///
/// This can be executed as:
///
/// ```
/// coz::end!("foo");
/// ```
#[macro_export]
macro_rules! end {
    ($name:expr) => {{
        static COUNTER: $crate::Counter = $crate::Counter::end($name);
        COUNTER.increment();
    }};
}

/// Marks a lexical scope with `coz::begin!` and `coz::end!` which are executed
/// even on early exit (e.g. via `return`, `?` or `panic!`).
///
/// Where this macro is invoked is where a `begin` counter is placed, and then
/// at the end of the lexical scope (when this macro's local variable goes out
/// of scope) an `end` counter is placed.
///
/// # Examples
///
/// ```rust
/// coz::scope!("outer");
/// {
///     coz::scope!("inner");
/// }
/// ```
#[macro_export]
macro_rules! scope {
    ($name:expr) => {
        static BEGIN_COUNTER: $crate::Counter = $crate::Counter::begin($name);
        static END_COUNTER: $crate::Counter = $crate::Counter::end($name);
        BEGIN_COUNTER.increment();
        let _coz_scope_guard = $crate::Guard::new(&END_COUNTER);
    };
}

/// Perform one-time per-thread initialization for `coz`.
///
/// This may not be necessary to call, but for good measure it's recommended to
/// call once per thread in your application near where the thread starts.
/// If you run into issues with segfaults related to SIGPROF handlers this may
/// help fix the issue since it installs a bigger stack earlier on in the
/// process.
pub fn thread_init() {
    // As one-time program initialization, make sure that our sigaltstack is big
    // enough. By default coz uses SIGPROF on an alternate signal stack, but the
    // Rust standard library already sets up a SIGALTSTACK which is
    // unfortunately too small to run coz's handler. If our sigaltstack looks
    // too small let's allocate a bigger one and use it here.
    thread_local!(static SIGALTSTACK_DISABLED: Cell<bool> = Cell::new(false));
    if SIGALTSTACK_DISABLED.with(|s| s.replace(true)) {
        return;
    }
    unsafe {
        let mut stack = mem::zeroed();
        libc::sigaltstack(ptr::null(), &mut stack);
        let size = 1 << 20; // 1mb
        if stack.ss_size >= size {
            return;
        }
        let ss_sp = libc::mmap(
            ptr::null_mut(),
            size,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_PRIVATE | libc::MAP_ANON,
            -1,
            0,
        );
        if ss_sp == libc::MAP_FAILED {
            panic!("failed to allocate alternative stack");
        }
        let new_stack = libc::stack_t {
            ss_sp,
            ss_flags: 0,
            ss_size: size,
        };
        libc::sigaltstack(&new_stack, ptr::null_mut());
    }
}

/// A `coz`-counter which is either intended for throughput or `begin`/`end`
/// points.
///
/// This is typically created by macros above via `progress!()`, `begin!()`, or
/// `end!()`, but if necessary you can also create one of these manually in your
/// own application for your own macros.
pub struct Counter {
    slot: OnceCell<Option<&'static coz_counter_t>>,
    ty: libc::c_int,
    name: &'static str,
}

const COZ_COUNTER_TYPE_THROUGHPUT: libc::c_int = 1;
const COZ_COUNTER_TYPE_BEGIN: libc::c_int = 2;
const COZ_COUNTER_TYPE_END: libc::c_int = 3;

impl Counter {
    /// Creates a throughput coz counter with the given name.
    pub const fn progress(name: &'static str) -> Counter {
        Counter::new(COZ_COUNTER_TYPE_THROUGHPUT, name)
    }

    /// Creates a latency coz counter with the given name, used for when an
    /// operation begins.
    ///
    /// Note that this counter should be paired with an `end` counter of the
    /// same name.
    pub const fn begin(name: &'static str) -> Counter {
        Counter::new(COZ_COUNTER_TYPE_BEGIN, name)
    }

    /// Creates a latency coz counter with the given name, used for when an
    /// operation ends.
    ///
    /// Note that this counter should be paired with an `begin` counter of the
    /// same name.
    pub const fn end(name: &'static str) -> Counter {
        Counter::new(COZ_COUNTER_TYPE_END, name)
    }

    const fn new(ty: libc::c_int, name: &'static str) -> Counter {
        Counter {
            slot: OnceCell::new(),
            ty,
            name,
        }
    }

    /// Increment that an operation happened on this counter.
    ///
    /// For throughput counters this should be called in a location where you
    /// want something to happen more often.
    ///
    /// For latency-based counters this should be called before and after the
    /// operation you'd like to measure the latency of.
    pub fn increment(&self) {
        let counter = self.slot.get_or_init(|| self.create_counter());
        if let Some(counter) = counter {
            assert_eq!(
                mem::size_of_val(&counter.count),
                mem::size_of::<libc::size_t>()
            );
            counter.count.fetch_add(1, SeqCst);
        }
    }

    fn create_counter(&self) -> Option<&'static coz_counter_t> {
        let name = CString::new(self.name).unwrap();
        let ptr = coz_get_counter(self.ty, &name);
        if ptr.is_null() {
            None
        } else {
            Some(unsafe { &*ptr })
        }
    }
}

/// A type that increments a counter on drop. This allows us to issue the right
/// coz calls to `begin` and `end` for the duration of a scope, regardless of how
/// the scope was exited (e.g. by early return, `?` or panic).
pub struct Guard<'t> {
    counter: &'t Counter
}

impl<'t> Guard<'t> {
    pub fn new(counter: &'t Counter) -> Self {
        Guard {
            counter
        }
    }
}

impl<'t> Drop for Guard<'t> {
    fn drop(&mut self) {
        self.counter.increment();
    }
}

#[repr(C)]
struct coz_counter_t {
    count: AtomicUsize,
    backoff: libc::size_t,
}

#[cfg(target_os = "linux")]
fn coz_get_counter(ty: libc::c_int, name: &CStr) -> *mut coz_counter_t {
    static PTR: AtomicUsize = AtomicUsize::new(1);
    let mut ptr = PTR.load(SeqCst);
    if ptr == 1 {
        let name = CStr::from_bytes_with_nul(b"_coz_get_counter\0").unwrap();
        ptr = unsafe { libc::dlsym(libc::RTLD_DEFAULT, name.as_ptr() as *const _) as usize };
        PTR.store(ptr, SeqCst);
    }
    if ptr == 0 {
        return ptr::null_mut();
    }

    thread_init(); // just in case we haven't already

    unsafe {
        mem::transmute::<
            usize,
            unsafe extern "C" fn(libc::c_int, *const libc::c_char) -> *mut coz_counter_t,
        >(ptr)(ty, name.as_ptr())
    }
}

#[cfg(not(target_os = "linux"))]
fn coz_get_counter(_ty: libc::c_int, _name: &CStr) -> *mut coz_counter_t {
    ptr::null_mut()
}
