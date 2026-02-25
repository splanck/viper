/* redundant_stress.c — Redundant computation / constant propagation benchmark (500K iterations).
   Equivalent to examples/il/benchmarks/redundant_stress.il */
#include <stdint.h>
#include <stdlib.h>

int main(void)
{
    int64_t sum = 0;
    for (int64_t i = 0; i < 500000; ++i) {
        /* Constant expressions: SCCP folds these to immediate constants. */
        int64_t k1 = 10 + 20;
        int64_t k2 = k1 * 3;
        int64_t k3 = k2 - 40;

        /* Redundant subexpressions: computed identically twice. */
        int64_t a1 = i + 7;
        int64_t a2 = a1 * 3;

        int64_t b1 = i + 7;
        int64_t b2 = b1 * 3;

        /* More constant folding chains. */
        int64_t c1 = 100 + 200;
        int64_t c2 = c1 * 2;
        int64_t c3 = c2 - 100;

        /* Third constant chain. */
        int64_t d1 = 5 + 10;
        int64_t d2 = d1 * 5;
        int64_t d3 = d2 - 5;

        /* Live computation that uses the redundant pair and constants. */
        int64_t live = a2 + b2 + k3 + c3 + d3;

        /* Keep sum within range. */
        int64_t raw_sum = sum + live;
        sum = raw_sum & 268435455;
    }
    return (int)(sum & 0xFF);
}
