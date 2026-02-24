/* arith_stress.c — Arithmetic-heavy loop benchmark (500K iterations).
   Equivalent to examples/il/benchmarks/arith_stress.il */
#include <stdint.h>
#include <stdlib.h>

int main(void)
{
    int64_t sum = 0;
    for (int64_t i = 0; i < 500000; ++i) {
        int64_t t1 = i + 1;
        int64_t t2 = t1 * 2;
        int64_t t3 = i + 3;
        int64_t t4 = t2 + t3;
        int64_t t5 = t4 * 5;
        int64_t t6 = t5 - i;
        int64_t t7 = t6 + 7;
        int64_t t8 = t7 * 3;
        int64_t t9 = t8 - 11;
        sum += t9;
    }
    return (int)(sum & 0xFF);
}
