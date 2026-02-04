//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/common/RuntimeComponents.hpp
// Purpose: Common runtime component classification for native code linking.
// Key invariants: Symbol prefix mappings must be kept in sync with runtime
//                 library organization in src/runtime/CMakeLists.txt.
// Links: src/tools/viper/cmd_codegen_arm64.cpp
//        src/codegen/x86_64/CodegenPipeline.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string_view>

namespace viper::codegen
{

/// @brief Runtime library components for selective linking.
/// @details Native backends use these to determine which runtime archives
///          to link based on symbols referenced in generated assembly.
enum class RtComponent
{
    Base,        ///< Core runtime (always linked)
    Arrays,      ///< Array operations (rt_arr_*)
    Oop,         ///< Object-oriented features (rt_obj_*, rt_type_*, etc.)
    Collections, ///< Collections and containers (rt_list_*, rt_map_*, rt_grid2d_*, rt_timer_*, etc.)
    Text,        ///< Text processing (rt_codec_*, rt_csv_*, etc.)
    IoFs,        ///< File I/O (rt_file_*, rt_dir_*, etc.)
    Exec,        ///< Process execution (rt_exec_*, rt_machine_*)
    Threads,     ///< Threading (rt_monitor_*, rt_thread_*, etc.)
    Graphics,    ///< Graphics (rt_canvas_*, rt_color_*, etc.)
};

/// @brief Map a runtime symbol to its component for selective linking.
/// @param sym The symbol name (e.g., "rt_list_add", "rt_grid2d_new").
/// @return The component if recognized, std::nullopt otherwise.
/// @note Keep this in sync with src/runtime/CMakeLists.txt library organization.
inline std::optional<RtComponent> componentForRuntimeSymbol(std::string_view sym)
{
    auto starts = [&](std::string_view prefix) -> bool {
        return sym.size() >= prefix.size() && sym.substr(0, prefix.size()) == prefix;
    };

    // Arrays component
    if (starts("rt_arr_"))
        return RtComponent::Arrays;

    // OOP component
    if (starts("rt_obj_") || starts("rt_type_") || starts("rt_cast_") || starts("rt_ns_") ||
        sym == "rt_bind_interface")
        return RtComponent::Oop;

    // Collections component (includes game dev utilities: Grid2D, Timer, SmoothValue)
    if (starts("rt_list_") || starts("rt_map_") || starts("rt_treemap_") || starts("rt_bag_") ||
        starts("rt_queue_") || starts("rt_ring_") || starts("rt_seq_") || starts("rt_stack_") ||
        starts("rt_bytes_") || starts("rt_grid2d_") || starts("rt_timer_") || starts("rt_smoothvalue_") ||
        starts("rt_inputmanager_") || starts("rt_inputaction_"))
        return RtComponent::Collections;

    // Text component
    if (starts("rt_codec_") || starts("rt_csv_") || starts("rt_guid_") || starts("rt_hash_") ||
        starts("rt_parse_"))
        return RtComponent::Text;

    // I/O and filesystem component
    if (starts("rt_file_") || starts("rt_dir_") || starts("rt_path_") ||
        starts("rt_binfile_") || starts("rt_linereader_") || starts("rt_linewriter_") ||
        starts("rt_io_file_") || sym == "rt_eof_ch" || sym == "rt_lof_ch" ||
        sym == "rt_loc_ch" || sym == "rt_close_err" || sym == "rt_seek_ch_err" ||
        sym == "rt_write_ch_err" || sym == "rt_println_ch_err" ||
        sym == "rt_line_input_ch_err" || sym == "rt_open_err_vstr")
        return RtComponent::IoFs;

    // Exec component
    if (starts("rt_exec_") || starts("rt_machine_"))
        return RtComponent::Exec;

    // Threads component
    if (starts("rt_monitor_") || starts("rt_thread_") || starts("rt_safe_"))
        return RtComponent::Threads;

    // Graphics component
    if (starts("rt_canvas_") || starts("rt_color_") || starts("rt_vec2_") ||
        starts("rt_vec3_") || starts("rt_pixels_"))
        return RtComponent::Graphics;

    return std::nullopt;
}

} // namespace viper::codegen
