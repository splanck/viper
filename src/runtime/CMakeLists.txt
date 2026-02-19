set(RT_BASE_SOURCES
        core/rt_context.c
        core/rt_crc32.c
        core/rt_error.c
        core/rt_trap.c
        core/rt_stack_safety.c
        core/rt_fp.c
        core/rt_memory.c
        core/rt_output.c
        core/rt_string_ops.c
        core/rt_string_builder.c
        core/rt_string_format.c
        core/rt_string_encode.c
        core/rt_string_intern.c
        core/rt_io.c
        core/rt_math.c
        core/rt_perlin.c
        core/rt_random.c
        core/rt_bits.c
        core/rt_pool.c
        core/rt_heap.c
        core/rt_numeric.c
        core/rt_numeric_conv.c
        core/rt_bigint.c
        core/rt_debug.c
        core/rt_fmt.c
        core/rt_format.c
        core/rt_int_format.c
        core/rt_printf_compat.c
        core/rt_term.c
        core/rt_time.c
        core/rt_datetime.c
        core/rt_dateonly.c
        core/rt_easing.c
        core/rt_duration.c
        core/rt_reltime.c
        core/rt_daterange.c
        core/rt_stopwatch.c
        core/rt_countdown.c
        core/rt_modvar.c
        core/rt_args.c
        core/rt_log.c
        core/rt_msgbus.c
        core/rt_gc.c
)

set(RT_ARRAY_SOURCES
        arrays/rt_array.c
        arrays/rt_array_i64.c
        arrays/rt_array_f64.c
        arrays/rt_array_str.c
        arrays/rt_array_obj.c
        arrays/rt_list_i64.c
)

set(RT_OOP_SOURCES
        oop/rt_box.c
        oop/rt_object.c
        oop/rt_oop_dispatch.c
        oop/rt_type_registry.c
        oop/rt_ns_bridge.c
        oop/rt_exc.c
        oop/rt_result.c
        oop/rt_option.c
        oop/rt_lazy.c
        oop/rt_lazyseq.c
)

set(RT_COLLECTIONS_SOURCES
        collections/rt_bag.c
        collections/rt_convert_coll.c
        collections/rt_iter.c
        collections/rt_bitset.c
        collections/rt_deque.c
        collections/rt_list.c
        collections/rt_pqueue.c
        collections/rt_seq.c
        collections/rt_seq_functional.c
        collections/rt_set.c
        collections/rt_sortedset.c
        collections/rt_stack.c
        collections/rt_queue.c
        collections/rt_ring.c
        collections/rt_lrucache.c
        collections/rt_bloomfilter.c
        collections/rt_unionfind.c
        collections/rt_orderedmap.c
        collections/rt_defaultmap.c
        collections/rt_frozenset.c
        collections/rt_frozenmap.c
        collections/rt_sparsearray.c
        collections/rt_weakmap.c
        collections/rt_map.c
        collections/rt_intmap.c
        collections/rt_bimap.c
        collections/rt_countmap.c
        collections/rt_multimap.c
        collections/rt_treemap.c
        collections/rt_trie.c
        collections/rt_bytes.c
        collections/rt_binbuf.c
        collections/rt_grid2d.c
        collections/rt_timer.c
        collections/rt_statemachine.c
        collections/rt_tween.c
        collections/rt_buttongroup.c
        collections/rt_smoothvalue.c
        collections/rt_particle.c
        collections/rt_spriteanim.c
        collections/rt_collision.c
        collections/rt_objpool.c
        collections/rt_screenfx.c
        collections/rt_pathfollow.c
        collections/rt_quadtree.c
)

set(RT_TEXT_SOURCES
        text/rt_codec.c
        text/rt_csv.c
        text/rt_ini.c
        text/rt_guid.c
        text/rt_hash.c
        text/rt_json.c
        text/rt_json_stream.c
        text/rt_xml.c
        text/rt_yaml.c
        text/rt_keyderive.c
        text/rt_aes.c
        text/rt_cipher.c
        text/rt_password.c
        text/rt_rand.c
        text/rt_regex.c
        text/rt_compiled_pattern.c
        text/rt_scanner.c
        text/rt_template.c
        text/rt_textwrap.c
        text/rt_diff.c
        text/rt_numfmt.c
        text/rt_parse.c
        text/rt_pluralize.c
        text/rt_version.c
        text/rt_toml.c
        text/rt_markdown.c
        text/rt_jsonpath.c
        text/rt_html.c
        text/rt_serialize.c
)

set(RT_IO_FS_SOURCES
        io/rt_file.c
        io/rt_file_io.c
        io/rt_file_path.c
        io/rt_file_ext.c
        io/rt_binfile.c
        io/rt_memstream.c
        io/rt_stream.c
        io/rt_linereader.c
        io/rt_linewriter.c
        io/rt_path.c
        io/rt_dir.c
        io/rt_glob.c
        io/rt_tempfile.c
        io/rt_watcher.c
        io/rt_compress.c
        io/rt_archive.c
)

set(RT_EXEC_SOURCES
        system/rt_exec.c
        system/rt_machine.c
)

set(RT_GRAPHICS_SOURCES
        graphics/rt_action.c
        graphics/rt_font.c
        graphics/rt_graphics.c
        graphics/rt_gui_app.c
        graphics/rt_gui_codeeditor.c
        graphics/rt_gui_features.c
        graphics/rt_gui_menus.c
        graphics/rt_gui_system.c
        graphics/rt_gui_widgets.c
        graphics/rt_gui_widgets_complex.c
        graphics/rt_input.c
        graphics/rt_input_pad.c
        graphics/rt_inputmgr.c
        graphics/rt_keychord.c
        graphics/rt_mat3.c
        graphics/rt_mat4.c
        graphics/rt_vec2.c
        graphics/rt_vec3.c
        graphics/rt_quat.c
        graphics/rt_spline.c
        graphics/rt_physics2d.c
        graphics/rt_pixels.c
        graphics/rt_sprite.c
        graphics/rt_spritesheet.c
        graphics/rt_camera.c
        graphics/rt_tilemap.c
        graphics/rt_scene.c
        graphics/rt_spritebatch.c
)

set(RT_AUDIO_SOURCES
        audio/rt_audio.c
        audio/rt_playlist.c
)

set(RT_THREADS_SOURCES
        threads/rt_async.c
        threads/rt_channel.c
        threads/rt_concmap.c
        threads/rt_future.c
        threads/rt_monitor.c
        threads/rt_parallel.c
        threads/rt_safe_i64.c
        threads/rt_threads.c
        threads/rt_threadpool.c
        threads/rt_threads_primitives.cpp
        threads/rt_concqueue.c
        threads/rt_cancellation.c
        threads/rt_debounce.c
        threads/rt_scheduler.c
)

set(RT_NETWORK_SOURCES
        network/rt_network.c
        network/rt_network_http.c
        network/rt_restclient.c
        network/rt_retry.c
        network/rt_ratelimit.c
        network/rt_websocket.c
        network/rt_crypto.c
        network/rt_tls.c
)

set(RT_PUBLIC_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_action.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_context.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_error.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_trap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_stack_safety.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_fp.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_math.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_numeric.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_random.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_string.h
        ${CMAKE_CURRENT_SOURCE_DIR}/arrays/rt_array.h
        ${CMAKE_CURRENT_SOURCE_DIR}/arrays/rt_array_i64.h
        ${CMAKE_CURRENT_SOURCE_DIR}/arrays/rt_array_f64.h
        ${CMAKE_CURRENT_SOURCE_DIR}/arrays/rt_array_str.h
        ${CMAKE_CURRENT_SOURCE_DIR}/arrays/rt_array_obj.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_heap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/oop/rt_box.h
        ${CMAKE_CURRENT_SOURCE_DIR}/oop/rt_object.h
        ${CMAKE_CURRENT_SOURCE_DIR}/oop/rt_oop.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_debug.h
        ${CMAKE_CURRENT_SOURCE_DIR}/io/rt_file.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_fmt.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_format.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_int_format.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_log.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_printf_compat.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_string_builder.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_output.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_modvar.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_args.h
        ${CMAKE_CURRENT_SOURCE_DIR}/oop/rt_ns_bridge.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_font.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_graphics.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_gui.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_input.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_inputmgr.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_grid2d.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_timer.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_statemachine.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_tween.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_buttongroup.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_smoothvalue.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_particle.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_spriteanim.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_collision.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_objpool.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_screenfx.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_pathfollow.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_quadtree.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core/rt_datetime.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_map.h
        ${CMAKE_CURRENT_SOURCE_DIR}/collections/rt_binbuf.h
        ${CMAKE_CURRENT_SOURCE_DIR}/text/rt_parse.h
        ${CMAKE_CURRENT_SOURCE_DIR}/io/rt_path.h
        ${CMAKE_CURRENT_SOURCE_DIR}/io/rt_dir.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_pixels.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_sprite.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_camera.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_tilemap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_scene.h
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics/rt_spritebatch.h
        ${CMAKE_CURRENT_SOURCE_DIR}/audio/rt_audio.h
        ${CMAKE_CURRENT_SOURCE_DIR}/threads/rt_threads.h
        ${CMAKE_CURRENT_SOURCE_DIR}/network/rt_network.h
)

find_package(Threads REQUIRED)

add_library(viper_rt_base_obj OBJECT ${RT_BASE_SOURCES})
add_library(viper_rt_arrays_obj OBJECT ${RT_ARRAY_SOURCES})
add_library(viper_rt_oop_obj OBJECT ${RT_OOP_SOURCES})
add_library(viper_rt_collections_obj OBJECT ${RT_COLLECTIONS_SOURCES})
add_library(viper_rt_text_obj OBJECT ${RT_TEXT_SOURCES})
add_library(viper_rt_io_fs_obj OBJECT ${RT_IO_FS_SOURCES})
add_library(viper_rt_exec_obj OBJECT ${RT_EXEC_SOURCES})
add_library(viper_rt_graphics_obj OBJECT ${RT_GRAPHICS_SOURCES})
add_library(viper_rt_audio_obj OBJECT ${RT_AUDIO_SOURCES})
add_library(viper_rt_threads_obj OBJECT ${RT_THREADS_SOURCES})
add_library(viper_rt_network_obj OBJECT ${RT_NETWORK_SOURCES})

add_library(viper_rt_base STATIC $<TARGET_OBJECTS:viper_rt_base_obj>)
add_library(viper_rt_arrays STATIC $<TARGET_OBJECTS:viper_rt_arrays_obj>)
add_library(viper_rt_oop STATIC $<TARGET_OBJECTS:viper_rt_oop_obj>)
add_library(viper_rt_collections STATIC $<TARGET_OBJECTS:viper_rt_collections_obj>)
add_library(viper_rt_text STATIC $<TARGET_OBJECTS:viper_rt_text_obj>)
add_library(viper_rt_io_fs STATIC $<TARGET_OBJECTS:viper_rt_io_fs_obj>)
add_library(viper_rt_exec STATIC $<TARGET_OBJECTS:viper_rt_exec_obj>)
add_library(viper_rt_graphics STATIC $<TARGET_OBJECTS:viper_rt_graphics_obj>)
add_library(viper_rt_audio STATIC $<TARGET_OBJECTS:viper_rt_audio_obj>)
add_library(viper_rt_threads STATIC $<TARGET_OBJECTS:viper_rt_threads_obj>)
add_library(viper_rt_network STATIC $<TARGET_OBJECTS:viper_rt_network_obj>)

add_library(viper_runtime STATIC
        $<TARGET_OBJECTS:viper_rt_base_obj>
        $<TARGET_OBJECTS:viper_rt_arrays_obj>
        $<TARGET_OBJECTS:viper_rt_oop_obj>
        $<TARGET_OBJECTS:viper_rt_collections_obj>
        $<TARGET_OBJECTS:viper_rt_text_obj>
        $<TARGET_OBJECTS:viper_rt_io_fs_obj>
        $<TARGET_OBJECTS:viper_rt_exec_obj>
        $<TARGET_OBJECTS:viper_rt_graphics_obj>
        $<TARGET_OBJECTS:viper_rt_audio_obj>
        $<TARGET_OBJECTS:viper_rt_threads_obj>
        $<TARGET_OBJECTS:viper_rt_network_obj>
)

set(VIPER_RUNTIME_COMPONENT_OBJECT_LIBS
        viper_rt_base_obj
        viper_rt_arrays_obj
        viper_rt_oop_obj
        viper_rt_collections_obj
        viper_rt_text_obj
        viper_rt_io_fs_obj
        viper_rt_exec_obj
        viper_rt_graphics_obj
        viper_rt_audio_obj
        viper_rt_threads_obj
        viper_rt_network_obj
)

set(VIPER_RUNTIME_COMPONENT_LIBS
        viper_rt_base
        viper_rt_arrays
        viper_rt_oop
        viper_rt_collections
        viper_rt_text
        viper_rt_io_fs
        viper_rt_exec
        viper_rt_graphics
        viper_rt_audio
        viper_rt_threads
        viper_rt_network
        viper_runtime
)

# Subdirectory list for include-path extension (all consumers get flat #include "rt_*.h" lookup)
set(_RT_SUBDIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/arrays
        ${CMAKE_CURRENT_SOURCE_DIR}/oop
        ${CMAKE_CURRENT_SOURCE_DIR}/collections
        ${CMAKE_CURRENT_SOURCE_DIR}/text
        ${CMAKE_CURRENT_SOURCE_DIR}/io
        ${CMAKE_CURRENT_SOURCE_DIR}/system
        ${CMAKE_CURRENT_SOURCE_DIR}/graphics
        ${CMAKE_CURRENT_SOURCE_DIR}/audio
        ${CMAKE_CURRENT_SOURCE_DIR}/threads
        ${CMAKE_CURRENT_SOURCE_DIR}/network
)

foreach (_rtobj IN LISTS VIPER_RUNTIME_COMPONENT_OBJECT_LIBS)
    target_include_directories(${_rtobj} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}    # root: rt_internal.h, rt_platform.h
            ${_RT_SUBDIRS}
            ${PROJECT_SOURCE_DIR}/include
            ${PROJECT_SOURCE_DIR}/src
    )
    if (MSVC)
        # Function-level linking (enables dead code elimination when supported by the linker)
        target_compile_options(${_rtobj} PRIVATE /Gy)
    else ()
        target_compile_options(${_rtobj} PRIVATE -ffunction-sections -fdata-sections)
    endif ()
    if (NOT WIN32)
        target_compile_definitions(${_rtobj} PRIVATE _POSIX_C_SOURCE=200809L)
    endif ()
endforeach ()

foreach (_rtlib IN LISTS VIPER_RUNTIME_COMPONENT_LIBS)
    target_include_directories(${_rtlib}
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/arrays>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/oop>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/collections>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/text>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/io>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/system>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/graphics>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/audio>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/threads>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/network>
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper_runtime>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper/runtime>
    )
endforeach ()

target_link_libraries(viper_rt_arrays PUBLIC viper_rt_oop viper_rt_base)
target_link_libraries(viper_rt_oop PUBLIC viper_rt_base)
target_link_libraries(viper_rt_collections PUBLIC viper_rt_oop viper_rt_base)
target_link_libraries(viper_rt_text PUBLIC viper_rt_collections viper_rt_base)
target_link_libraries(viper_rt_io_fs PUBLIC viper_rt_collections viper_rt_oop viper_rt_base)
target_link_libraries(viper_rt_exec PUBLIC viper_rt_collections viper_rt_base)
target_link_libraries(viper_rt_graphics PUBLIC viper_rt_oop viper_rt_base)
target_link_libraries(viper_rt_threads PUBLIC viper_rt_oop viper_rt_base)
target_link_libraries(viper_rt_network PUBLIC viper_rt_collections viper_rt_base)

# Windows requires ws2_32 for sockets
if (WIN32)
    target_link_libraries(viper_rt_network PUBLIC ws2_32)
    target_link_libraries(viper_runtime PUBLIC ws2_32)
    target_link_libraries(viper_rt_graphics PUBLIC Xinput9_1_0)
    target_link_libraries(viper_runtime PUBLIC Xinput9_1_0)
endif ()

if (NOT WIN32)
    # -lm is an implementation detail of the runtime; PRIVATE prevents it from
    # leaking into consumers' link lines (MED-7).
    target_link_libraries(viper_runtime PRIVATE m)
    target_link_libraries(viper_rt_base PRIVATE m)
endif ()

# MSVC release linker flags: /OPT:REF removes unreferenced functions/data,
# /OPT:ICF merges identical COMDAT sections — equivalent to GCC/Clang
# --gc-sections + --icf=all (MED-6).
if (MSVC)
    target_link_options(viper_runtime PRIVATE
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>)
endif ()

if (APPLE)
    target_link_libraries(viper_rt_graphics PUBLIC "-framework IOKit" "-framework CoreFoundation")
    target_link_libraries(viper_runtime PUBLIC "-framework IOKit" "-framework CoreFoundation")
endif ()

target_link_libraries(viper_rt_threads PUBLIC Threads::Threads)
target_link_libraries(viper_runtime PUBLIC Threads::Threads)

# VIPER_RC_DEBUG can be manually enabled for refcount debugging:
# cmake -DCMAKE_CXX_FLAGS="-DVIPER_RC_DEBUG=1" ...

install(FILES ${RT_PUBLIC_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper_runtime
)

install(FILES ${PROJECT_SOURCE_DIR}/include/viper/runtime/rt.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/runtime
)

# Graphics support (optional, enabled by default)
option(VIPER_ENABLE_GRAPHICS "Enable ViperGFX graphics support" ON)

if (VIPER_ENABLE_GRAPHICS AND TARGET vipergfx)
    message(STATUS "viper_runtime: Graphics support enabled")
    target_compile_definitions(viper_rt_graphics_obj PRIVATE VIPER_ENABLE_GRAPHICS)
    target_include_directories(viper_rt_graphics_obj PRIVATE
            ${PROJECT_SOURCE_DIR}/src/lib/graphics/include
            ${PROJECT_SOURCE_DIR}/src/lib/gui/include
    )
    target_link_libraries(viper_rt_graphics PUBLIC vipergfx)
    target_link_libraries(viper_runtime PUBLIC vipergfx)
    # Link GUI library if available
    if (TARGET vipergui)
        target_link_libraries(viper_rt_graphics PRIVATE vipergui)
        target_link_libraries(viper_runtime PRIVATE vipergui)
    endif ()
else ()
    message(STATUS "viper_runtime: Graphics support disabled (vipergfx not available)")
endif ()

# Audio support (optional, enabled by default)
option(VIPER_ENABLE_AUDIO "Enable ViperAUD audio support" ON)

if (VIPER_ENABLE_AUDIO AND TARGET viperaud)
    message(STATUS "viper_runtime: Audio support enabled")
    target_compile_definitions(viper_rt_audio_obj PRIVATE VIPER_ENABLE_AUDIO)
    target_include_directories(viper_rt_audio_obj PRIVATE
            ${PROJECT_SOURCE_DIR}/src/lib/audio/include
    )
    target_link_libraries(viper_rt_audio PUBLIC viperaud)
    target_link_libraries(viper_runtime PUBLIC viperaud)
else ()
    message(STATUS "viper_runtime: Audio support disabled (viperaud not available)")
endif ()
