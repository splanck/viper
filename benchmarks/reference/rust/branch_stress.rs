fn main() {
    let mut count: i64 = 0;
    for i in 0..200000_i64 {
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
