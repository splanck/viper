/* inline_stress — Inlining stress benchmark (500K iterations).
   Equivalent to examples/il/benchmarks/inline_stress.il */
using System;

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

    static int Main()
    {
        long sum = 0;
        for (long i = 0; i < 500000; ++i)
        {
            long r = Combine(i);
            long rawSum = sum + r;
            sum = rawSum & 268435455;
        }
        return (int)(sum & 0xFF);
    }
}
