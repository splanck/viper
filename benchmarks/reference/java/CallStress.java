/** CallStress.java — Function call overhead benchmark (10M iterations).
    Equivalent to examples/il/benchmarks/call_stress.il */
public class CallStress {
    static long addTriple(long a, long b, long c) {
        return a + b + c;
    }

    static long mulPair(long x, long y) {
        return x * y;
    }

    static long compute(long n) {
        long a = n;
        long b = n + 1;
        long c = n + 2;
        long sum = addTriple(a, b, c);
        return mulPair(sum, 3);
    }

    public static void main(String[] args) {
        long n = 10000000 + args.length;
        long sum = 0;
        for (long i = 0; i < n; i++) {
            long r1 = compute(i);
            long r2 = addTriple(i, r1, 1);
            long r3 = mulPair(r2, 2);
            sum += r3;
        }
        System.exit((int)(sum & 0xFF));
    }
}
