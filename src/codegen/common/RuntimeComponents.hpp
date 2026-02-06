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
#include <unordered_set>
#include <vector>

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
    Collections, ///< Collections and containers (rt_list_*, rt_map_*, rt_grid2d_*, rt_timer_*,
                 ///< etc.)
    Text,        ///< Text processing (rt_codec_*, rt_csv_*, etc.)
    IoFs,        ///< File I/O (rt_file_*, rt_dir_*, etc.)
    Exec,        ///< Process execution (rt_exec_*, rt_machine_*)
    Threads,     ///< Threading (rt_monitor_*, rt_thread_*, etc.)
    Graphics,    ///< Graphics (rt_canvas_*, rt_color_*, etc.)
    Audio,       ///< Audio (rt_audio_*, rt_playlist_*)
    Network,     ///< Network (rt_network_*, rt_restclient_*, etc.)
};

/// @brief Map a runtime symbol to its component for selective linking.
/// @param sym The symbol name (e.g., "rt_list_add", "rt_grid2d_new").
/// @return The component if recognized, std::nullopt otherwise.
/// @note Keep this in sync with src/runtime/CMakeLists.txt library organization.
inline std::optional<RtComponent> componentForRuntimeSymbol(std::string_view sym)
{
    auto starts = [&](std::string_view prefix) -> bool
    { return sym.size() >= prefix.size() && sym.substr(0, prefix.size()) == prefix; };

    // Arrays component
    if (starts("rt_arr_"))
        return RtComponent::Arrays;

    // OOP component
    if (starts("rt_obj_") || starts("rt_type_") || starts("rt_cast_") || starts("rt_ns_") ||
        starts("rt_box_") || starts("rt_exc_") || starts("rt_result_") || starts("rt_option_") ||
        starts("rt_lazy") || starts("rt_oop_") || sym == "rt_bind_interface")
        return RtComponent::Oop;

    // Collections component (includes game dev utilities)
    if (starts("rt_list_") || starts("rt_map_") || starts("rt_treemap_") || starts("rt_bag_") ||
        starts("rt_queue_") || starts("rt_ring_") || starts("rt_seq_") || starts("rt_stack_") ||
        starts("rt_bytes_") || starts("rt_grid2d_") || starts("rt_timer_") ||
        starts("rt_smoothvalue_") || starts("rt_inputmanager_") || starts("rt_inputaction_") ||
        starts("rt_set_") || starts("rt_sortedset_") || starts("rt_deque_") ||
        starts("rt_bitset_") || starts("rt_bloomfilter_") || starts("rt_bimap_") ||
        starts("rt_countmap_") || starts("rt_defaultmap_") || starts("rt_frozenset_") ||
        starts("rt_frozenmap_") || starts("rt_lrucache_") || starts("rt_multimap_") ||
        starts("rt_orderedmap_") || starts("rt_sparsearray_") || starts("rt_weakmap_") ||
        starts("rt_pqueue_") || starts("rt_trie_") || starts("rt_unionfind_") ||
        starts("rt_convert_") || starts("rt_statemachine_") || starts("rt_tween_") ||
        starts("rt_buttongroup_") || starts("rt_particle_") || starts("rt_spriteanim_") ||
        starts("rt_collision_") || starts("rt_objpool_") || starts("rt_screenfx_") ||
        starts("rt_pathfollow_") || starts("rt_quadtree_"))
        return RtComponent::Collections;

    // Text component
    if (starts("rt_codec_") || starts("rt_csv_") || starts("rt_guid_") || starts("rt_hash_") ||
        starts("rt_parse_") || starts("rt_json") || starts("rt_xml_") || starts("rt_yaml_") ||
        starts("rt_ini_") || starts("rt_toml_") || starts("rt_html_") || starts("rt_markdown_") ||
        starts("rt_regex_") || starts("rt_compiled_pattern_") || starts("rt_scanner_") ||
        starts("rt_template_") || starts("rt_textwrap_") || starts("rt_diff_") ||
        starts("rt_numfmt_") || starts("rt_pluralize_") || starts("rt_version_") ||
        starts("rt_keyderive_") || starts("rt_aes_") || starts("rt_cipher_") ||
        starts("rt_password_") || starts("rt_rand_"))
        return RtComponent::Text;

    // I/O and filesystem component
    if (starts("rt_file_") || starts("rt_dir_") || starts("rt_path_") || starts("rt_binfile_") ||
        starts("rt_linereader_") || starts("rt_linewriter_") || starts("rt_io_file_") ||
        starts("rt_memstream_") || starts("rt_stream_") || starts("rt_watcher_") ||
        starts("rt_compress_") || starts("rt_archive_") || starts("rt_glob_") ||
        starts("rt_tempfile_") || sym == "rt_eof_ch" || sym == "rt_lof_ch" || sym == "rt_loc_ch" ||
        sym == "rt_close_err" || sym == "rt_seek_ch_err" || sym == "rt_write_ch_err" ||
        sym == "rt_println_ch_err" || sym == "rt_line_input_ch_err" || sym == "rt_open_err_vstr")
        return RtComponent::IoFs;

    // Exec component
    if (starts("rt_exec_") || starts("rt_machine_"))
        return RtComponent::Exec;

    // Threads component
    if (starts("rt_monitor_") || starts("rt_thread_") || starts("rt_safe_") ||
        starts("rt_channel_") || starts("rt_future_") || starts("rt_parallel_") ||
        starts("rt_concqueue_") || starts("rt_cancellation_") || starts("rt_debounce_") ||
        starts("rt_scheduler_") || starts("rt_pool_"))
        return RtComponent::Threads;

    // Graphics component
    if (starts("rt_canvas_") || starts("rt_color_") || starts("rt_vec2_") || starts("rt_vec3_") ||
        starts("rt_pixels_") || starts("rt_sprite_") || starts("rt_spritebatch_") ||
        starts("rt_tilemap_") || starts("rt_camera_") || starts("rt_scene_") ||
        starts("rt_font_") || starts("rt_gui_") || starts("rt_checkbox_") ||
        starts("rt_codeeditor_") || starts("rt_widget_") || starts("rt_treeview_") ||
        starts("rt_radiobutton_") || starts("rt_menuitem_") || starts("rt_contextmenu_") ||
        starts("rt_statusbar_") || starts("rt_toolbar_") || starts("rt_findbar_") ||
        starts("rt_commandpalette_") || starts("rt_scrollview_") || starts("rt_action_") ||
        starts("rt_input_") || starts("rt_inputmgr_") || starts("rt_mat3_") || starts("rt_mat4_") ||
        starts("rt_graphics_"))
        return RtComponent::Graphics;

    // Audio component
    if (starts("rt_audio_") || starts("rt_playlist_") || starts("rt_sound_") ||
        starts("rt_music_") || starts("rt_voice_"))
        return RtComponent::Audio;

    // Network component
    if (starts("rt_network_") || starts("rt_restclient_") || starts("rt_retry_") ||
        starts("rt_ratelimit_") || starts("rt_websocket_") || starts("rt_crypto_") ||
        starts("rt_tls_") || starts("rt_http_") || starts("rt_tcp_") || starts("rt_udp_"))
        return RtComponent::Network;

    // Base component (time, math, formatting, etc.)
    if (starts("rt_context_") || starts("rt_crc32_") || starts("rt_error_") || starts("rt_trap_") ||
        starts("rt_fp_") || starts("rt_memory_") || starts("rt_string_") || starts("rt_io_") ||
        starts("rt_math_") || starts("rt_perlin_") || starts("rt_random_") || starts("rt_bits_") ||
        starts("rt_numeric_") || starts("rt_bigint_") || starts("rt_debug_") || starts("rt_fmt_") ||
        starts("rt_format_") || starts("rt_int_format_") || starts("rt_printf_") ||
        starts("rt_term_") || starts("rt_time_") || starts("rt_datetime_") ||
        starts("rt_dateonly_") || starts("rt_daterange_") || starts("rt_duration_") ||
        starts("rt_reltime_") || starts("rt_stopwatch_") || starts("rt_countdown_") ||
        starts("rt_easing_") || starts("rt_modvar_") || starts("rt_args_") || starts("rt_log_") ||
        starts("rt_msgbus_") || starts("rt_heap_") || starts("rt_output_"))
        return RtComponent::Base;

    return std::nullopt;
}

/// @brief Get the static library archive name for a runtime component.
/// @param comp The runtime component.
/// @return Library base name (e.g., "viper_rt_collections").
inline std::string_view archiveNameForComponent(RtComponent comp)
{
    switch (comp)
    {
        case RtComponent::Base:
            return "viper_rt_base";
        case RtComponent::Arrays:
            return "viper_rt_arrays";
        case RtComponent::Oop:
            return "viper_rt_oop";
        case RtComponent::Collections:
            return "viper_rt_collections";
        case RtComponent::Text:
            return "viper_rt_text";
        case RtComponent::IoFs:
            return "viper_rt_io_fs";
        case RtComponent::Exec:
            return "viper_rt_exec";
        case RtComponent::Threads:
            return "viper_rt_threads";
        case RtComponent::Graphics:
            return "viper_rt_graphics";
        case RtComponent::Audio:
            return "viper_rt_audio";
        case RtComponent::Network:
            return "viper_rt_network";
    }
    return "viper_rt_base";
}

/// @brief Resolve the full set of required runtime components from referenced symbols.
/// @param symbols The runtime symbols referenced by generated code.
/// @return Ordered list of required components (with dependencies resolved).
///         Base is always included first.
template <typename SymbolRange>
inline std::vector<RtComponent> resolveRequiredComponents(const SymbolRange &symbols)
{
    // Classify symbols into components.
    std::unordered_set<int> needed;
    for (const auto &sym : symbols)
    {
        const auto comp = componentForRuntimeSymbol(sym);
        if (comp)
            needed.insert(static_cast<int>(*comp));
    }

    auto has = [&](RtComponent c) { return needed.count(static_cast<int>(c)) != 0; };
    auto add = [&](RtComponent c) { needed.insert(static_cast<int>(c)); };

    // Apply dependency rules (internal runtime calls between components).
    if (has(RtComponent::Text) || has(RtComponent::IoFs) || has(RtComponent::Exec) ||
        has(RtComponent::Network))
        add(RtComponent::Collections);
    if (has(RtComponent::Collections))
        add(RtComponent::Arrays);
    if (has(RtComponent::Collections) || has(RtComponent::Arrays) || has(RtComponent::Graphics) ||
        has(RtComponent::Threads) || has(RtComponent::Audio) || has(RtComponent::Network))
        add(RtComponent::Oop);

    // Build ordered list (Base always first).
    std::vector<RtComponent> result;
    result.push_back(RtComponent::Base);

    // Add remaining components in a stable order.
    static constexpr RtComponent order[] = {
        RtComponent::Oop,
        RtComponent::Arrays,
        RtComponent::Collections,
        RtComponent::Text,
        RtComponent::IoFs,
        RtComponent::Exec,
        RtComponent::Threads,
        RtComponent::Graphics,
        RtComponent::Audio,
        RtComponent::Network,
    };
    for (auto c : order)
    {
        if (has(c))
            result.push_back(c);
    }
    return result;
}

} // namespace viper::codegen
