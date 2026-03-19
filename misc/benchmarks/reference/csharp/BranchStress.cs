/* branch_stress — Branch-heavy loop benchmark (20M iterations).
   Equivalent to examples/il/benchmarks/branch_stress.il */
using System;
using System.Runtime.CompilerServices;

class BranchStress
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static long Run(long n)
    {
        long count = 0;
        for (long i = 0; i < n; ++i)
        {
            if (i % 2 == 0) count += 1;
            if (i % 3 == 0) count += 2;
            if (i % 5 == 0) count += 3;
            if (i % 7 == 0) count += 5;
        }
        return count;
    }

    static int Main(string[] args)
    {
        long result = Run(20000000 + args.Length);
        return (int)(result & 0xFF);
    }
}
