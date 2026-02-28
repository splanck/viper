/* redundant_stress — Redundant computation / constant propagation benchmark (500K iterations).
   Equivalent to examples/il/benchmarks/redundant_stress.il */
using System;

class RedundantStress
{
    static int Main()
    {
        long sum = 0;
        for (long i = 0; i < 500000; ++i)
        {
            long k1 = 10 + 20;
            long k2 = k1 * 3;
            long k3 = k2 - 40;

            long a1 = i + 7;
            long a2 = a1 * 3;

            long b1 = i + 7;
            long b2 = b1 * 3;

            long c1 = 100 + 200;
            long c2 = c1 * 2;
            long c3 = c2 - 100;

            long d1 = 5 + 10;
            long d2 = d1 * 5;
            long d3 = d2 - 5;

            long live = a2 + b2 + k3 + c3 + d3;

            long rawSum = sum + live;
            sum = rawSum & 268435455;
        }
        return (int)(sum & 0xFF);
    }
}
