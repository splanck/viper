fn add_triple(a: i64, b: i64, c: i64) -> i64 {
    a + b + c
}

fn mul_pair(x: i64, y: i64) -> i64 {
    x * y
}

fn compute(n: i64) -> i64 {
    mul_pair(add_triple(n, n + 1, n + 2), 3)
}

fn main() {
    let mut sum: i64 = 0;
    for i in 0..100000_i64 {
        let r1 = compute(i);
        let r2 = add_triple(i, r1, 1);
        let r3 = mul_pair(r2, 2);
        sum += r3;
    }
    std::process::exit((sum & 0xFF) as i32);
}
