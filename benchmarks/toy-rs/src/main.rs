#[allow(unused)]
use coz;
use std::thread;
use std::hint::black_box;

pub fn a_first_fn() -> i64 {
    let mut v: i64 = 0;
    for x in 0..200_000_000 {
        // Make the computations opaque so the optimizer can't cancel them out.
        let t1 = black_box(x * x);
        v += t1;

        let t2 = black_box(x * x);
        v -= t2;

        let t3 = black_box(x * x);
        v += t3;

        let t4 = black_box(x * x);
        v -= t4;
    }

    // Also make the final value opaque to discourage further folding.
    black_box(v)
}

pub fn b_second_fn() -> i64 {
    let mut v: i64 = 0;
    for x in 0..100_000_000 {
        let t1 = black_box(x * x);
        v += t1;

        let t2 = black_box(x * x);
        v -= t2;

        let t3 = black_box(x * x);
        v += t3;

        let t4 = black_box(x * x);
        v -= t4;
    }

    black_box(v)
}

fn main() {
    for _n in 1..100 {
        let handle1 = thread::spawn(|| a_first_fn());
        let handle2 = thread::spawn(|| b_second_fn());

        let _ = handle1.join().unwrap();
        let _ = handle2.join().unwrap();

        coz::progress!();
    }
}
