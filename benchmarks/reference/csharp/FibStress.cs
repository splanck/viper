/* fib_stress — Recursive fibonacci(40) benchmark.
   Equivalent to examples/il/benchmarks/fib_stress.il */
using System;
using System.Runtime.CompilerServices;

class FibStress
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static long Fib(long n)
    {
        if (n <= 1) return n;
        return Fib(n - 1) + Fib(n - 2);
    }

    static int Main(string[] args)
    {
        long result = Fib(40 + args.Length);
        return (int)(result & 0xFF);
    }
}
