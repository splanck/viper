//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_gltf_loader.cpp
// Purpose: libFuzzer harness for the glTF/GLB preload parser.
//
// Key invariants:
//   - Input size is capped before allocation.
//   - The preload API owns the copied root bytes on both success and failure.
//
// Ownership/Lifetime:
//   - Successful preload bundles are released before returning to libFuzzer.
//
// Links: src/runtime/graphics/3d/assets/rt_gltf.h, fuzz_3d_helpers.hpp
//
//===----------------------------------------------------------------------===//

#include "fuzz_3d_helpers.hpp"
#include "rt_gltf.h"

#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!zanna_fuzz3d::input_is_bounded(size) || size == 0)
        return 0;

    uint8_t *copy = static_cast<uint8_t *>(std::malloc(size));
    if (!copy)
        return 0;
    std::memcpy(copy, data, size);

    char error[256];
    rt_gltf_preload_bundle *bundle =
        rt_gltf_preload_bundle_create_cstr("fuzz.gltf", copy, size, 0, error, sizeof(error));
    if (bundle)
        rt_gltf_preload_bundle_free(bundle);
    return 0;
}
