set(RT_BASE_SOURCES
        rt_context.c
        rt_crc32.c
        rt_error.c
        rt_trap.c
        rt_stack_safety.c
        rt_fp.c
        rt_memory.c
        rt_output.c
        rt_string_ops.c
        rt_string_builder.c
        rt_string_format.c
        rt_string_encode.c
        rt_io.c
        rt_math.c
        rt_random.c
        rt_bits.c
        rt_pool.c
        rt_heap.c
        rt_numeric.c
        rt_numeric_conv.c
        rt_debug.c
        rt_fmt.c
        rt_format.c
        rt_int_format.c
        rt_printf_compat.c
        rt_term.c
        rt_time.c
        rt_datetime.c
        rt_stopwatch.c
        rt_countdown.c
        rt_modvar.c
        rt_args.c
        rt_log.c
)

set(RT_ARRAY_SOURCES
        rt_array.c
        rt_array_i64.c
        rt_array_f64.c
        rt_array_str.c
        rt_array_obj.c
)

set(RT_OOP_SOURCES
        rt_box.c
        rt_object.c
        rt_oop_dispatch.c
        rt_type_registry.c
        rt_ns_bridge.c
        rt_exc.c
)

set(RT_COLLECTIONS_SOURCES
        rt_bag.c
        rt_list.c
        rt_pqueue.c
        rt_seq.c
        rt_stack.c
        rt_queue.c
        rt_ring.c
        rt_map.c
        rt_treemap.c
        rt_bytes.c
)

set(RT_TEXT_SOURCES
        rt_codec.c
        rt_csv.c
        rt_guid.c
        rt_hash.c
        rt_json.c
        rt_keyderive.c
        rt_aes.c
        rt_rand.c
        rt_regex.c
        rt_template.c
        rt_parse.c
)

set(RT_IO_FS_SOURCES
        rt_file.c
        rt_file_io.c
        rt_file_path.c
        rt_file_ext.c
        rt_binfile.c
        rt_memstream.c
        rt_linereader.c
        rt_linewriter.c
        rt_path.c
        rt_dir.c
        rt_watcher.c
        rt_compress.c
        rt_archive.c
)

set(RT_EXEC_SOURCES
        rt_exec.c
        rt_machine.c
)

set(RT_GRAPHICS_SOURCES
        rt_font.c
        rt_graphics.c
        rt_gui.c
        rt_input.c
        rt_input_pad.c
        rt_vec2.c
        rt_vec3.c
        rt_pixels.c
        rt_sprite.c
        rt_camera.c
        rt_tilemap.c
)

set(RT_AUDIO_SOURCES
        rt_audio.c
)

set(RT_THREADS_SOURCES
        rt_monitor.c
        rt_safe_i64.c
        rt_threads.c
        rt_threads_primitives.cpp
)

set(RT_NETWORK_SOURCES
        rt_network.c
        rt_network_http.c
        rt_crypto.c
        rt_tls.c
)

set(RT_PUBLIC_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_context.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_error.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_trap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_stack_safety.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_fp.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_math.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_numeric.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_random.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_string.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_array.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_array_i64.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_array_f64.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_array_str.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_array_obj.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_heap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_box.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_object.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_oop.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_debug.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_file.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_fmt.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_format.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_int_format.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_log.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_printf_compat.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_string_builder.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_output.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_modvar.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_args.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_ns_bridge.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_font.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_graphics.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_gui.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_input.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_datetime.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_map.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_parse.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_path.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_dir.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_pixels.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_sprite.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_camera.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_tilemap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_audio.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_threads.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_network.h
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

foreach (_rtobj IN LISTS VIPER_RUNTIME_COMPONENT_OBJECT_LIBS)
    target_include_directories(${_rtobj} PRIVATE
            ${PROJECT_SOURCE_DIR}/src/runtime
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
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/runtime>
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
    target_link_libraries(viper_runtime PUBLIC m)
    target_link_libraries(viper_rt_base PUBLIC m)
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
