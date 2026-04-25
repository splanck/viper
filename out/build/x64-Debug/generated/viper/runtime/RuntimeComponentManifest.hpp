//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: generated/viper/runtime/RuntimeComponentManifest.hpp
// Purpose: Generated runtime component/archive manifest shared by codegen and
//          audit tooling.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <string_view>

namespace viper::runtime_manifest {

inline constexpr std::array<std::string_view, 13>
    kRuntimeComponentArchives = {
        "viper_rt_base",
        "viper_rt_arrays",
        "viper_rt_oop",
        "viper_rt_collections",
        "viper_rt_game",
        "viper_rt_text",
        "viper_rt_io_fs",
        "viper_rt_exec",
        "viper_rt_threads",
        "viper_rt_graphics",
        "viper_rt_audio",
        "viper_rt_network",
        "viper_rt_localization",
};

} // namespace viper::runtime_manifest
