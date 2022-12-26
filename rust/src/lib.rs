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
use std::ffi::{CStr, CString};
use std::mem;
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
        static COUNTER: $crate::Counter = $crate::Counter::progress(concat!(file!(), ":", line!()));
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
        match ptr {
            // SAFETY: Pointer to counter returned by `coz_get_counter` is not null and aligned.
            Some(ptr) if !ptr.is_null() => Some(unsafe { &*ptr }),
            _ => None,
        }
    }
}

/// A type that increments a counter on drop. This allows us to issue the right
/// coz calls to `begin` and `end` for the duration of a scope, regardless of how
/// the scope was exited (e.g. by early return, `?` or panic).
pub struct Guard<'t> {
    counter: &'t Counter,
}

impl<'t> Guard<'t> {
    pub fn new(counter: &'t Counter) -> Self {
        Guard { counter }
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

/// The type of `_coz_get_counter` as defined in `include/coz.h`
///
/// `typedef coz_counter_t* (*coz_get_counter_t)(int, const char*);`
type GetCounterFn = unsafe extern "C" fn(libc::c_int, *const libc::c_char) -> *mut coz_counter_t;

#[cfg(target_os = "linux")]
fn coz_get_counter(ty: libc::c_int, name: &CStr) -> Option<*mut coz_counter_t> {
    static GET_COUNTER: OnceCell<Option<GetCounterFn>> = OnceCell::new();
    let func = GET_COUNTER.get_or_init(|| {
        let name = CStr::from_bytes_with_nul(b"_coz_get_counter\0").unwrap();
        // SAFETY: We are calling an external function that does exist in Linux.
        // No specific invariants that we must uphold have been defined.
        let func = unsafe { libc::dlsym(libc::RTLD_DEFAULT, name.as_ptr()) };
        if func.is_null() {
            None
        } else {
            // SAFETY: If the pointer returned by dlsym is not null it is a valid pointer to the function
            // identified by the provided symbol. The type of `_coz_get_counter` is defined in `include/coz.h`
            // as [GetCounterFn].
            Some(unsafe { mem::transmute(func) })
        }
    });

    // SAFETY: We are calling an external function which exists as it is not None
    // No specific invariants that we must uphold have been defined.
    func.map(|f| unsafe { f(ty, name.as_ptr()) })
}

#[cfg(not(target_os = "linux"))]
fn coz_get_counter(_ty: libc::c_int, _name: &CStr) -> Option<*mut coz_counter_t> {
    None
}
