/* call_stress — Function call overhead benchmark (100K iterations).
   Equivalent to examples/il/benchmarks/call_stress.il */
using System;
using System.Runtime.CompilerServices;

class CallStress
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static long AddTriple(long a, long b, long c)
    {
        return a + b + c;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static long MulPair(long x, long y)
    {
        return x * y;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static long Compute(long n)
    {
        long a = n;
        long b = n + 1;
        long c = n + 2;
        long sum = AddTriple(a, b, c);
        return MulPair(sum, 3);
    }

    static int Main()
    {
        long sum = 0;
        for (long i = 0; i < 100000; ++i)
        {
            long r1 = Compute(i);
            long r2 = AddTriple(i, r1, 1);
            long r3 = MulPair(r2, 2);
            sum += r3;
        }
        return (int)(sum & 0xFF);
    }
}
