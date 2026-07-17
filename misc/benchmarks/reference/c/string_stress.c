//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: misc/benchmarks/reference/c/string_stress.c
// Purpose: C reference for the string_stress benchmark kernel.
// Key invariants:
//   - Standalone translation unit; no cross-layer dependencies.
// Ownership/Lifetime:
//   - No long-lived state; all allocations are scoped to the run.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/* string_stress.c — String manipulation benchmark (500K iterations).
   Equivalent to examples/il/benchmarks/string_stress.il */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    (void)argv;
    int64_t n = 500000 + (argc - 1);
    int64_t sum = 0;
    for (int64_t i = 0; i < n; ++i) {
        char buf[64];
        /* Build "Hello World!" by concatenation */
        strcpy(buf, "Hello");
        strcat(buf, " ");
        strcat(buf, "World");
        strcat(buf, "!");
        sum += (int64_t)strlen(buf);
    }
    return (int)(sum & 0xFF);
}
