/** FibStress.java — Recursive fibonacci(40) benchmark.
    Equivalent to examples/il/benchmarks/fib_stress.il */
public class FibStress {
    static long fib(long n) {
        if (n <= 1) return n;
        return fib(n - 1) + fib(n - 2);
    }

    public static void main(String[] args) {
        long result = fib(40 + args.length);
        System.exit((int)(result & 0xFF));
    }
}
