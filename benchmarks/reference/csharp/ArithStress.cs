/* arith_stress — Arithmetic-heavy loop benchmark (500K iterations).
   Equivalent to examples/il/benchmarks/arith_stress.il */
using System;

class ArithStress
{
    static int Main()
    {
        long sum = 0;
        for (long i = 0; i < 500000; ++i)
        {
            long t1 = i + 1;
            long t2 = t1 * 2;
            long t3 = i + 3;
            long t4 = t2 + t3;
            long t5 = t4 * 5;
            long t6 = t5 - i;
            long t7 = t6 + 7;
            long t8 = t7 * 3;
            long t9 = t8 - 11;
            sum += t9;
        }
        return (int)(sum & 0xFF);
    }
}
