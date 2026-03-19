/** InlineStress.java — Inlining stress benchmark (50M iterations).
    Equivalent to examples/il/benchmarks/inline_stress.il */
public class InlineStress {
    static long doubleVal(long x) {
        return x + x;
    }

    static long square(long x) {
        return x * x;
    }

    static long add3(long a, long b, long c) {
        return a + b + c;
    }

    static long inc(long x) {
        return x + 1;
    }

    static long combine(long x) {
        long d = doubleVal(x);
        long s = square(x);
        long i = inc(x);
        return add3(d, s, i);
    }

    public static void main(String[] args) {
        long n = 50000000 + args.length;
        long sum = 0;
        for (long i = 0; i < n; i++) {
            long r = combine(i);
            long rawSum = sum + r;
            sum = rawSum & 268435455;
        }
        System.exit((int)(sum & 0xFF));
    }
}
