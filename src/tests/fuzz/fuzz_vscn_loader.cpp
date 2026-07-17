//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_vscn_loader.cpp
// Purpose: libFuzzer harness for SceneGraph .vscn loading.
//
// Key invariants:
//   - Input size is capped before spilling to a temporary file.
//   - Malformed scenes must return NULL with diagnostics, never trap.
//
// Ownership/Lifetime:
//   - Temporary files and returned SceneGraph handles are released each iteration.
//
// Links: src/runtime/graphics/3d/scene/rt_scene3d.h, fuzz_3d_helpers.hpp
//
//===----------------------------------------------------------------------===//

#include "fuzz_3d_helpers.hpp"
#include "rt_scene3d.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!zanna_fuzz3d::input_is_bounded(size))
        return 0;

    std::string path = zanna_fuzz3d::write_temp_asset("zanna_fuzz_vscn_", ".vscn", data, size);
    if (path.empty())
        return 0;
    rt_asset_error_clear();
    rt_string runtime_path = zanna_fuzz3d::runtime_string_from_path(path);
    if (runtime_path) {
        void *scene = rt_scene3d_load(runtime_path);
        zanna_fuzz3d::release_runtime_object(scene);
        rt_string_unref(runtime_path);
    }
    zanna_fuzz3d::remove_temp_asset(path);

    /* Also exercise the in-memory entry point, which has its own length handling
     * before delegating to the shared parser (rt_scene3d_load_from_memory). */
    rt_asset_error_clear();
    void *mem_scene =
        rt_scene3d_load_from_memory(nullptr, reinterpret_cast<const char *>(data), size);
    zanna_fuzz3d::release_runtime_object(mem_scene);
    return 0;
}
