fn main() {
    let mut sum: i64 = 0;
    for _ in 0..50000_i64 {
        let result = "Hello".to_string() + " " + "World" + "!";
        sum += result.len() as i64;
    }
    std::process::exit((sum & 0xFF) as i32);
}
