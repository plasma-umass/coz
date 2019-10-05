#[test]
fn smoke() {
    coz::progress!();
    coz::progress!("foo");
    coz::begin!("foo");
    coz::end!("foo");
}

#[test]
fn smoke_scoped() {
    coz::scope!("scope");
    let mut x = 1u32;
    x = x + 1;
    assert!(x == 2);
}
