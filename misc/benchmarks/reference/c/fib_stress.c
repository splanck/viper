//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: misc/benchmarks/reference/c/fib_stress.c
// Purpose: C reference for the fib_stress benchmark kernel.
// Key invariants:
//   - Standalone translation unit; no cross-layer dependencies.
// Ownership/Lifetime:
//   - No long-lived state; all allocations are scoped to the run.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/* fib_stress.c — Recursive fibonacci(40) benchmark.
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
    int64_t result = fib(40);
    return (int)(result & 0xFF);
}
