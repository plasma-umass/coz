const A: usize = 2_000_000_000;
const B: usize = (A as f64 * 1.2) as usize;

fn main() {
    let a = std::thread::spawn(move || {
        for _ in 0..A {
            coz::progress!("a");
        }
    });
    let b = std::thread::spawn(move || {
        for _ in 0..B {
            coz::progress!("b");
        }
    });
    a.join().unwrap();
    b.join().unwrap();
}
