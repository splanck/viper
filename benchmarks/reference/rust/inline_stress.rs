fn double(x: i64) -> i64 {
    x + x
}

fn square(x: i64) -> i64 {
    x * x
}

fn add3(a: i64, b: i64, c: i64) -> i64 {
    a + b + c
}

fn inc(x: i64) -> i64 {
    x + 1
}

fn combine(x: i64) -> i64 {
    add3(double(x), square(x), inc(x))
}

fn main() {
    let mut sum: i64 = 0;
    for i in 0..500000_i64 {
        let r = combine(i);
        let raw_sum = sum + r;
        sum = raw_sum & 268435455;
    }
    std::process::exit((sum & 0xFF) as i32);
}
