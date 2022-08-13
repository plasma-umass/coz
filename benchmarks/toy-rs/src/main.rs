#[allow(unused)]
use coz;
use std::thread;

fn black_box<T>(dummy: T) -> T {
    unsafe {
        std::ptr::read_volatile(&dummy)
    }
}

#[no_mangle]
pub fn a_first_fn() -> i64 {
    let mut v: i64 = 0;
    for x in 0..200000000 {
	v += x * x;
	v -= x * x;
	v += x * x;
	v -= x * x;
    }
    return v;
}

#[no_mangle]
pub fn b_second_fn() -> i64 {
    let mut v: i64 = 0;
    for x in 0..100000000 {
	v += x * x;
	v -= x * x;
	v += x * x;
	v -= x * x;
    }
    return v;
}

fn main() {
    for _n in 1..100 {
      let handle1 = thread::spawn(|| a_first_fn());
      let handle2 = thread::spawn(|| b_second_fn());
      handle1.join().unwrap();
      handle2.join().unwrap();
      coz::progress!();
    }
}
