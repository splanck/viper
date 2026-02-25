/* udiv_stress.c — Unsigned division stress benchmark (500K iterations).
   Equivalent to examples/il/benchmarks/udiv_stress.il */
#include <stdint.h>
#include <stdlib.h>

int main(void)
{
    int64_t sum = 0;
    for (int64_t i = 1; i < 500001; ++i) {
        /* Divide by ascending powers of 2. */
        int64_t d1 = i / 2;
        int64_t d2 = i / 4;
        int64_t d3 = i / 8;
        int64_t d4 = i / 16;
        int64_t d5 = i / 32;
        int64_t d6 = i / 64;
        int64_t d7 = i / 128;
        int64_t d8 = i / 256;

        /* Accumulate all quotients so nothing is dead. */
        int64_t s7 = d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8;

        /* Keep sum within range by masking upper bits. */
        int64_t raw_sum = sum + s7;
        sum = raw_sum & 268435455;
    }
    return (int)(sum & 0xFF);
}
