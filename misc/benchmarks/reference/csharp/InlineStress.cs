/* inline_stress — Inlining stress benchmark (50M iterations).
   Equivalent to examples/il/benchmarks/inline_stress.il */
using System;
using System.Runtime.CompilerServices;

class InlineStress
{
    static long DoubleVal(long x) => x + x;
    static long Square(long x) => x * x;
    static long Add3(long a, long b, long c) => a + b + c;
    static long Inc(long x) => x + 1;

    static long Combine(long x)
    {
        long d = DoubleVal(x);
        long s = Square(x);
        long i = Inc(x);
        return Add3(d, s, i);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static long Run(long n)
    {
        long sum = 0;
        for (long i = 0; i < n; ++i)
        {
            long r = Combine(i);
            long rawSum = sum + r;
            sum = rawSum & 268435455;
        }
        return sum;
    }

    static int Main(string[] args)
    {
        long result = Run(50000000 + args.Length);
        return (int)(result & 0xFF);
    }
}
