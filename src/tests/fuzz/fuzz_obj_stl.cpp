//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_obj_stl.cpp
// Purpose: libFuzzer harness for Mesh3D OBJ and STL content loading.
//
// Key invariants:
//   - The same bounded byte stream is exercised through both mesh text/binary loaders.
//   - Loader failures are expected and ignored unless they crash.
//
// Ownership/Lifetime:
//   - Temporary files and returned Mesh3D handles are released each iteration.
//
// Links: src/runtime/graphics/3d/render/rt_canvas3d.h, fuzz_3d_helpers.hpp
//
//===----------------------------------------------------------------------===//

#include "fuzz_3d_helpers.hpp"
#include "rt_canvas3d.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!zanna_fuzz3d::input_is_bounded(size))
        return 0;

    std::string obj_path = zanna_fuzz3d::write_temp_asset("zanna_fuzz_obj_", ".obj", data, size);
    if (!obj_path.empty()) {
        rt_asset_error_clear();
        rt_string runtime_path = zanna_fuzz3d::runtime_string_from_path(obj_path);
        if (runtime_path) {
            void *mesh = rt_mesh3d_from_obj(runtime_path);
            zanna_fuzz3d::release_runtime_object(mesh);
            rt_string_unref(runtime_path);
        }
        zanna_fuzz3d::remove_temp_asset(obj_path);
    }

    std::string stl_path = zanna_fuzz3d::write_temp_asset("zanna_fuzz_stl_", ".stl", data, size);
    if (!stl_path.empty()) {
        rt_asset_error_clear();
        rt_string runtime_path = zanna_fuzz3d::runtime_string_from_path(stl_path);
        if (runtime_path) {
            void *mesh = rt_mesh3d_from_stl(runtime_path);
            zanna_fuzz3d::release_runtime_object(mesh);
            rt_string_unref(runtime_path);
        }
        zanna_fuzz3d::remove_temp_asset(stl_path);
    }
    return 0;
}
