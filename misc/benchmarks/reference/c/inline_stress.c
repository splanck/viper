//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: misc/benchmarks/reference/c/inline_stress.c
// Purpose: C reference for the inline_stress benchmark kernel.
// Key invariants:
//   - Standalone translation unit; no cross-layer dependencies.
// Ownership/Lifetime:
//   - No long-lived state; all allocations are scoped to the run.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/* inline_stress.c — Inlining stress benchmark (50M iterations).
   Equivalent to examples/il/benchmarks/inline_stress.il */
#include <stdint.h>
#include <stdlib.h>

static int64_t double_val(int64_t x)
{
    return x + x;
}

static int64_t square(int64_t x)
{
    return x * x;
}

static int64_t add3(int64_t a, int64_t b, int64_t c)
{
    return a + b + c;
}

static int64_t inc(int64_t x)
{
    return x + 1;
}

static int64_t combine(int64_t x)
{
    int64_t d = double_val(x);
    int64_t s = square(x);
    int64_t i = inc(x);
    return add3(d, s, i);
}

int main(int argc, char **argv)
{
    (void)argv;
    int64_t n = 50000000 + (argc - 1);
    int64_t sum = 0;
    for (int64_t i = 0; i < n; ++i) {
        int64_t r = combine(i);
        int64_t raw_sum = sum + r;
        sum = raw_sum & 268435455;
    }
    return (int)(sum & 0xFF);
}
