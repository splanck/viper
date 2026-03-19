fn fib(n: i64) -> i64 {
    if n <= 1 {
        n
    } else {
        fib(n - 1) + fib(n - 2)
    }
}

fn main() {
    let args_offset = std::env::args().count() as i64 - 1;
    let result = fib(40 + args_offset);
    std::process::exit((result & 0xFF) as i32);
}
