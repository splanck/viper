/* string_stress — String manipulation benchmark (500K iterations).
   Equivalent to examples/il/benchmarks/string_stress.il */
using System;
using System.Runtime.CompilerServices;

class StringStress
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static long Run(long n)
    {
        long sum = 0;
        for (long i = 0; i < n; ++i)
        {
            string buf = "Hello";
            buf += " ";
            buf += "World";
            buf += "!";
            sum += buf.Length;
        }
        return sum;
    }

    static int Main(string[] args)
    {
        long result = Run(500000 + args.Length);
        return (int)(result & 0xFF);
    }
}
