/* fib_stress — Recursive fibonacci(35) benchmark.
   Equivalent to examples/il/benchmarks/fib_stress.il */
using System;

class FibStress
{
    static long Fib(long n)
    {
        if (n <= 1) return n;
        return Fib(n - 1) + Fib(n - 2);
    }

    static int Main()
    {
        long result = Fib(35);
        return (int)(result & 0xFF);
    }
}
