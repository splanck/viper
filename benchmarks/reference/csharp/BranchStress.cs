/* branch_stress — Branch-heavy loop benchmark (200K iterations).
   Equivalent to examples/il/benchmarks/branch_stress.il */
using System;

class BranchStress
{
    static int Main()
    {
        long count = 0;
        for (long i = 0; i < 200000; ++i)
        {
            if (i % 2 == 0) count += 1;
            if (i % 3 == 0) count += 2;
            if (i % 5 == 0) count += 3;
            if (i % 7 == 0) count += 5;
        }
        return (int)(count & 0xFF);
    }
}
