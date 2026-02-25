fn main() {
    let mut sum: i64 = 0;
    for i in 1..500001_i64 {
        let d1 = i / 2;
        let d2 = i / 4;
        let d3 = i / 8;
        let d4 = i / 16;
        let d5 = i / 32;
        let d6 = i / 64;
        let d7 = i / 128;
        let d8 = i / 256;
        let s7 = d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8;
        let raw_sum = sum + s7;
        sum = raw_sum & 268435455;
    }
    std::process::exit((sum & 0xFF) as i32);
}
