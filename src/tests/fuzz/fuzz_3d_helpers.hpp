//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/fuzz/fuzz_3d_helpers.hpp
// Purpose: Shared helpers for libFuzzer harnesses that exercise 3D content loaders.
//
// Key invariants:
//   - Fuzzer inputs are capped before any runtime loader sees them.
//   - Temporary files are removed after each path-based loader invocation.
//
// Ownership/Lifetime:
//   - Helpers create short-lived filesystem entries and release runtime objects returned by
//   loaders.
//
// Links: fuzz_gltf_loader.cpp, fuzz_fbx_loader.cpp, fuzz_vscn_loader.cpp,
//        fuzz_obj_stl.cpp, fuzz_stream_manifest.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_asset_error.h"
#include "rt_string.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
int64_t rt_obj_release_check0(void *obj);
void rt_obj_free(void *obj);
}

namespace viper_fuzz3d {

constexpr size_t kMaxInputBytes = 256u * 1024u;

inline bool input_is_bounded(size_t size) {
    return size <= kMaxInputBytes;
}

inline rt_string runtime_string_from_path(const std::string &path) {
    return rt_string_from_bytes(path.c_str(), path.size());
}

inline void release_runtime_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

inline std::string write_temp_asset(const char *stem,
                                    const char *extension,
                                    const uint8_t *data,
                                    size_t size) {
    static std::atomic<uint64_t> next_id{0};

    try {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            (std::string(stem ? stem : "viper_fuzz3d_") +
             std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)) +
             (extension ? extension : ".bin"));
        std::ofstream out(path, std::ios::binary);
        if (!out)
            return {};
        if (size > 0)
            out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
        out.close();
        if (!out)
            return {};
        return path.string();
    } catch (...) {
        return {};
    }
}

inline void remove_temp_asset(const std::string &path) {
    if (path.empty())
        return;
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

} // namespace viper_fuzz3d
