//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_ktx2_loader.cpp
// Purpose: libFuzzer harness for the KTX2 texture parser, including the
//   Zstandard/ZLIB supercompression paths and every software block decoder
//   (BC1/BC3/BC4/BC5/BC7/ETC2/ASTC).
//
// Key invariants:
//   - Input size is capped before allocation.
//   - Malformed input must fail recoverably (rt_asset_error), never trap.
//
// Ownership/Lifetime:
//   - Successfully parsed assets are released before returning to libFuzzer.
//
// Links: src/runtime/graphics/3d/assets/rt_textureasset3d_ktx2.inc,
//   fuzz_3d_helpers.hpp
//
//===----------------------------------------------------------------------===//

#include "fuzz_3d_helpers.hpp"
#include "rt_textureasset3d.h"

#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!zanna_fuzz3d::input_is_bounded(size) || size == 0)
        return 0;

    void *asset = rt_textureasset3d_load_ktx2_memory(data, size);
    if (asset)
        zanna_fuzz3d::release_runtime_object(asset);
    return 0;
}
