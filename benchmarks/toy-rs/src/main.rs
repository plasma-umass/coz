#[allow(unused)]
use coz;
use std::thread;
#[no_mangle]
pub fn a_first_fn() {
    for _x in 0..2000000000 {}
}

#[no_mangle]
pub fn b_second_fn() {
    for _y in 0..1900000000 {}
}

fn main() {
    let handle1 = thread::spawn(|| a_first_fn());
    let handle2 = thread::spawn(|| b_second_fn());

    handle1.join().unwrap();
    handle2.join().unwrap();
    coz::progress!();
}
