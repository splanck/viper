// 20M iterations
fn main() {
    let args_offset = std::env::args().count() as i64 - 1;
    let mut count: i64 = 0;
    for i in 0..(20000000 + args_offset) {
        if i % 2 == 0 {
            count += 1;
        }
        if i % 3 == 0 {
            count += 2;
        }
        if i % 5 == 0 {
            count += 3;
        }
        if i % 7 == 0 {
            count += 5;
        }
    }
    std::process::exit((count & 0xFF) as i32);
}
