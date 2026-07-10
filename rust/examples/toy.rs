//! A Rust port of benchmarks/toy/toy.cpp: two threads run unequal amounts of
//! busy work, with a progress point after each unit of work. The busy loop in
//! `work` is the bottleneck coz should attribute experiments to. The loop is
//! written with `black_box` and explicit indexing so its hot lines belong to
//! this file rather than inlined std iterator code.

use std::hint::black_box;

const A_UNITS: u64 = 4_000_000;
const B_UNITS: u64 = (A_UNITS as f64 * 1.2) as u64;
const UNIT: u64 = 10_000;

fn work(n: u64) -> u64 {
    let mut x = 0u64;
    let mut i = 0u64;
    while i < n {
        x = black_box(x + i);
        i += 1;
    }
    x
}

fn main() {
    let a = std::thread::spawn(move || {
        for _ in 0..A_UNITS {
            black_box(work(UNIT));
            coz::progress!("a");
        }
    });
    let b = std::thread::spawn(move || {
        for _ in 0..B_UNITS {
            black_box(work(UNIT));
            coz::progress!("b");
        }
    });
    a.join().unwrap();
    b.join().unwrap();
}
