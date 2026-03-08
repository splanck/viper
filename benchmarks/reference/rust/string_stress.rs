// 500K iterations
fn main() {
    let args_offset = std::env::args().count() as i64 - 1;
    let mut sum: i64 = 0;
    for _ in 0..(500000 + args_offset) {
        let result = "Hello".to_string() + " " + "World" + "!";
        sum += result.len() as i64;
    }
    std::process::exit((sum & 0xFF) as i32);
}
