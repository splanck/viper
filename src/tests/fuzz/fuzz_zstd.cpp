//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_zstd.cpp
// Purpose: libFuzzer harness for the from-scratch Zstandard decompressor —
//   the FSE/Huffman/sequence machinery parses the most hostile-byte-dense
//   format in the runtime.
//
// Key invariants:
//   - The output budget is bounded so hostile size headers cannot OOM.
//   - Any input must either decode or fail cleanly; no crashes, no leaks.
//
// Ownership/Lifetime:
//   - Successful outputs are freed before returning to libFuzzer.
//
// Links: src/runtime/io/rt_zstd.c
//
//===----------------------------------------------------------------------===//

#include "rt_zstd.h"

#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > (256u << 10))
        return 0;

    uint8_t *out = nullptr;
    size_t out_len = 0;
    if (rt_zstd_decompress_raw(data, size, 1u << 22, &out, &out_len))
        std::free(out);
    return 0;
}
