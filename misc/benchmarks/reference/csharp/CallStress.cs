/* call_stress — Function call overhead benchmark (10M iterations).
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

    [MethodImpl(MethodImplOptions.NoInlining)]
    static long Run(long n)
    {
        long sum = 0;
        for (long i = 0; i < n; ++i)
        {
            long r1 = Compute(i);
            long r2 = AddTriple(i, r1, 1);
            long r3 = MulPair(r2, 2);
            sum += r3;
        }
        return sum;
    }

    static int Main(string[] args)
    {
        long result = Run(10000000 + args.Length);
        return (int)(result & 0xFF);
    }
}
