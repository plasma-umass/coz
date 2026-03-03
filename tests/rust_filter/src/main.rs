// Rust test binary for verifying that coz filters out Rust standard library
// paths from profiling results by default.
//
// This program deliberately uses stdlib types (HashMap, Vec, String) to ensure
// that inlined stdlib code appears in the binary's DWARF debug info, creating
// the conditions where stdlib lines would be profiled without proper filtering.

use std::collections::HashMap;

fn stdlib_heavy_work() -> usize {
    let mut map = HashMap::new();
    for i in 0..5000 {
        map.insert(i, format!("value_{}", i));
    }
    let mut total = 0usize;
    for (k, v) in &map {
        total = total.wrapping_add(*k);
        total = total.wrapping_add(v.len());
    }

    let mut vec: Vec<String> = map.into_values().collect();
    vec.sort();
    total = total.wrapping_add(vec.len());
    total
}

fn main() {
    let mut result = 0usize;
    // Run enough iterations for the profiler to collect samples
    for _ in 0..200 {
        result = result.wrapping_add(stdlib_heavy_work());
        coz::progress!();
    }
    // Prevent dead-code elimination
    println!("{}", result);
}
