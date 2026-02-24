/* call_stress.c — Function call overhead benchmark (100K iterations).
   Equivalent to examples/il/benchmarks/call_stress.il */
#include <stdint.h>
#include <stdlib.h>

static int64_t add_triple(int64_t a, int64_t b, int64_t c)
{
    return a + b + c;
}

static int64_t mul_pair(int64_t x, int64_t y)
{
    return x * y;
}

static int64_t compute(int64_t n)
{
    int64_t a = n;
    int64_t b = n + 1;
    int64_t c = n + 2;
    int64_t sum = add_triple(a, b, c);
    return mul_pair(sum, 3);
}

int main(void)
{
    int64_t sum = 0;
    for (int64_t i = 0; i < 100000; ++i) {
        int64_t r1 = compute(i);
        int64_t r2 = add_triple(i, r1, 1);
        int64_t r3 = mul_pair(r2, 2);
        sum += r3;
    }
    return (int)(sum & 0xFF);
}
