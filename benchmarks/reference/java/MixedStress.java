/** MixedStress.java — Mixed workload benchmark (10M iterations).
    Equivalent to examples/il/benchmarks/mixed_stress.il */
public class MixedStress {
    static long helper(long x) {
        return x * 3 + 7;
    }

    public static void main(String[] args) {
        long n = 10000000 + args.length;
        long sum = 0;
        for (long i = 0; i < n; i++) {
            long t1 = i + 1;
            long t2 = t1 * 2;
            long t3 = t2 - i;
            long tmp;
            if (i % 4 == 0) {
                long r1 = helper(t3);
                tmp = r1 * 2;
            } else {
                long r3 = t3 + 100;
                tmp = r3 * 3;
            }
            if (i % 7 == 0) {
                long bonusVal = helper(tmp);
                tmp = tmp + bonusVal;
            }
            sum += tmp;
        }
        System.exit((int)(sum & 0xFF));
    }
}
