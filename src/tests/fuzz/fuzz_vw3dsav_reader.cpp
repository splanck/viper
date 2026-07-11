//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_vw3dsav_reader.cpp
// Purpose: libFuzzer harness for the VW3DSAV1 world-persistence snapshot
//   validator (World3D.LoadState input surface).
//
// Key invariants:
//   - Input size is capped before the parse.
//   - The validator never traps, allocates unbounded memory, or reads out of
//     bounds regardless of input bytes.
//
// Ownership/Lifetime:
//   - The validator borrows the fuzzer buffer; nothing is retained.
//
// Links: src/runtime/graphics/3d/rt_game3d_persistence.c, ADR 0097
//
//===----------------------------------------------------------------------===//

#include "fuzz_3d_helpers.hpp"
#include "rt_game3d.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!viper_fuzz3d::input_is_bounded(size))
        return 0;
    (void)rt_game3d_persistence_validate(data, (int64_t)size);
    return 0;
}
