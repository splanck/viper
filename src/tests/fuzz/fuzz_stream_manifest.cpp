//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_stream_manifest.cpp
// Purpose: libFuzzer harness for Game3D world-stream manifest parsing.
//
// Key invariants:
//   - Input size is capped before spilling to a temporary manifest file.
//   - A single World3D/WorldStream3D pair is reused so parser fuzzing stays focused.
//
// Ownership/Lifetime:
//   - Temporary manifest files are removed each iteration; runtime globals live for process
//   duration.
//
// Links: src/runtime/graphics/3d/rt_game3d.h, fuzz_3d_helpers.hpp
//
//===----------------------------------------------------------------------===//

#include "fuzz_3d_helpers.hpp"
#include "rt_game3d.h"

namespace {

void *stream_for_fuzzing() {
    static void *world = nullptr;
    static void *stream = nullptr;

    if (stream)
        return stream;
    world = rt_game3d_world_new(rt_const_cstr("stream-fuzz"), 16, 16);
    if (!world)
        return nullptr;
    stream = rt_game3d_world_stream_new(world);
    return stream;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!zanna_fuzz3d::input_is_bounded(size))
        return 0;

    void *stream = stream_for_fuzzing();
    if (!stream)
        return 0;

    std::string path =
        zanna_fuzz3d::write_temp_asset("zanna_fuzz_stream_manifest_", ".json", data, size);
    if (path.empty())
        return 0;
    rt_asset_error_clear();
    rt_string runtime_path = zanna_fuzz3d::runtime_string_from_path(path);
    if (runtime_path) {
        rt_game3d_world_stream_mount_cells(stream, runtime_path);
        rt_game3d_world_stream_mount_tiled_terrain(stream, runtime_path);
        rt_string_unref(runtime_path);
    }
    zanna_fuzz3d::remove_temp_asset(path);
    return 0;
}
