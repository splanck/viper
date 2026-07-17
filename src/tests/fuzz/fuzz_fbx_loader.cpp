//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_fbx_loader.cpp
// Purpose: libFuzzer harness for FBX content loading.
//
// Key invariants:
//   - Input size is capped before spilling to a temporary file.
//   - Loader failures are expected and ignored unless they crash.
//
// Ownership/Lifetime:
//   - Temporary files and returned FBX asset handles are released each iteration.
//
// Links: src/runtime/graphics/3d/assets/rt_fbx_loader.h, fuzz_3d_helpers.hpp
//
//===----------------------------------------------------------------------===//

#include "fuzz_3d_helpers.hpp"
#include "rt_fbx_loader.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!zanna_fuzz3d::input_is_bounded(size))
        return 0;

    std::string path = zanna_fuzz3d::write_temp_asset("zanna_fuzz_fbx_", ".fbx", data, size);
    if (path.empty())
        return 0;
    rt_asset_error_clear();
    rt_string runtime_path = zanna_fuzz3d::runtime_string_from_path(path);
    if (runtime_path) {
        void *asset = rt_fbx_load(runtime_path);
        zanna_fuzz3d::release_runtime_object(asset);
        rt_string_unref(runtime_path);
    }
    zanna_fuzz3d::remove_temp_asset(path);
    return 0;
}
