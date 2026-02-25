fn main() {
    let mut sum: i64 = 0;
    for i in 0..500000_i64 {
        let k1 = 10 + 20;
        let k2 = k1 * 3;
        let k3 = k2 - 40;

        let a1 = i + 7;
        let a2 = a1 * 3;

        let b1 = i + 7;
        let b2 = b1 * 3;

        let c1 = 100 + 200;
        let c2 = c1 * 2;
        let c3 = c2 - 100;

        let d1 = 5 + 10;
        let d2 = d1 * 5;
        let d3 = d2 - 5;

        let live = a2 + b2 + k3 + c3 + d3;
        let raw_sum = sum + live;
        sum = raw_sum & 268435455;
    }
    std::process::exit((sum & 0xFF) as i32);
}
