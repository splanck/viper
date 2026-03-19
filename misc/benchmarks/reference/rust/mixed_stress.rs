// 10M iterations
fn helper(x: i64) -> i64 {
    x * 3 + 7
}

fn main() {
    let args_offset = std::env::args().count() as i64 - 1;
    let mut sum: i64 = 0;
    for i in 0..(10000000 + args_offset) {
        let t1 = i + 1;
        let t2 = t1 * 2;
        let t3 = t2 - i;
        let mut tmp;
        if i % 4 == 0 {
            tmp = helper(t3) * 2;
        } else {
            tmp = (t3 + 100) * 3;
        }
        if i % 7 == 0 {
            tmp = tmp + helper(tmp);
        }
        sum += tmp;
    }
    std::process::exit((sum & 0xFF) as i32);
}
