/* fib_stress.c — Recursive fibonacci(35) benchmark.
   Equivalent to examples/il/benchmarks/fib_stress.il */
#include <stdint.h>
#include <stdlib.h>

static int64_t fib(int64_t n)
{
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void)
{
    int64_t result = fib(35);
    return (int)(result & 0xFF);
}
