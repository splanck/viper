/* udiv_stress — Unsigned division stress benchmark (500K iterations).
   Equivalent to examples/il/benchmarks/udiv_stress.il */
using System;

class UdivStress
{
    static int Main()
    {
        long sum = 0;
        for (long i = 1; i < 500001; ++i)
        {
            long d1 = i / 2;
            long d2 = i / 4;
            long d3 = i / 8;
            long d4 = i / 16;
            long d5 = i / 32;
            long d6 = i / 64;
            long d7 = i / 128;
            long d8 = i / 256;

            long s7 = d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8;

            long rawSum = sum + s7;
            sum = rawSum & 268435455;
        }
        return (int)(sum & 0xFF);
    }
}
