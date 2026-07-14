//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_gltf_draco.cpp
// Purpose: libFuzzer harness for the KHR_draco_mesh_compression decoder.
//
// Key invariants:
//   - The Draco decoder must never trap, read out of bounds, or fail to terminate
//     on malformed input; it either decodes or reports failure and frees cleanly.
//   - This reaches draco_decode_mesh directly via rt_gltf_draco_decode_probe, which
//     the load_assets=0 glTF preload fuzzer (fuzz_gltf_loader) cannot exercise.
//
// Ownership/Lifetime:
//   - The probe frees any mesh it decodes; the harness owns nothing per iteration.
//
// Links: src/runtime/graphics/3d/assets/rt_gltf.h, rt_gltf_draco.inc, fuzz_3d_helpers.hpp
//
//===----------------------------------------------------------------------===//

#include "fuzz_3d_helpers.hpp"
#include "rt_gltf.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!viper_fuzz3d::input_is_bounded(size) || size == 0)
        return 0;
    (void)rt_gltf_draco_decode_probe(data, size);
    return 0;
}
