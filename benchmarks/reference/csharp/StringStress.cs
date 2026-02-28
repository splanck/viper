/* string_stress — String manipulation benchmark (50K iterations).
   Equivalent to examples/il/benchmarks/string_stress.il */
using System;

class StringStress
{
    static int Main()
    {
        long sum = 0;
        for (long i = 0; i < 50000; ++i)
        {
            string buf = "Hello";
            buf += " ";
            buf += "World";
            buf += "!";
            sum += buf.Length;
        }
        return (int)(sum & 0xFF);
    }
}
