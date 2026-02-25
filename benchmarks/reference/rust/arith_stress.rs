fn main() {
    let mut sum: i64 = 0;
    for i in 0..500000_i64 {
        let t1 = i + 1;
        let t2 = t1 * 2;
        let t3 = i + 3;
        let t4 = t2 + t3;
        let t5 = t4 * 5;
        let t6 = t5 - i;
        let t7 = t6 + 7;
        let t8 = t7 * 3;
        let t9 = t8 - 11;
        sum += t9;
    }
    std::process::exit((sum & 0xFF) as i32);
}
