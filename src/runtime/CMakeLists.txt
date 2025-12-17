set(RT_SOURCES
        rt_context.c
        rt_error.c
        rt_trap.c
        rt_fp.c
        rt_memory.c
        rt_output.c
        rt_string_ops.c
        rt_string_builder.c
        rt_string_format.c
        rt_string_encode.c
        rt_io.c
        rt_file.c
        rt_file_io.c
        rt_file_path.c
        rt_math.c
        rt_random.c
        rt_array.c
        rt_array_i64.c
        rt_array_f64.c
        rt_array_str.c
        rt_array_obj.c
        rt_bag.c
        rt_binfile.c
        rt_bits.c
        rt_linereader.c
        rt_linewriter.c
        rt_heap.c
        rt_numeric.c
        rt_numeric_conv.c
        rt_debug.c
        rt_format.c
        rt_int_format.c
        rt_printf_compat.c
        rt_term.c
        rt_time.c
        rt_object.c
        rt_oop_dispatch.c
        rt_type_registry.c
        rt_modvar.c
        rt_args.c
        rt_ns_bridge.c
        rt_file_ext.c
        rt_list.c
        rt_seq.c
        rt_stack.c
        rt_queue.c
        rt_ring.c
        rt_bytes.c
        rt_codec.c
        rt_graphics.c
        rt_datetime.c
        rt_exec.c
        rt_map.c
        rt_path.c
        rt_dir.c
        rt_stopwatch.c
        rt_guid.c
        rt_hash.c
        rt_machine.c
        rt_vec2.c
        rt_vec3.c
        rt_pixels.c
)

set(RT_PUBLIC_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_context.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_error.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_trap.h
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
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_object.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_oop.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_debug.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_file.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_format.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_int_format.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_printf_compat.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_string_builder.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_output.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_modvar.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_args.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_ns_bridge.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_graphics.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_datetime.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_map.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_path.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_dir.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_pixels.h
)

add_library(viper_runtime STATIC ${RT_SOURCES})
target_include_directories(viper_runtime
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/runtime>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper_runtime>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper/runtime>
        PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

if (NOT WIN32)
    target_link_libraries(viper_runtime PUBLIC m)
    target_compile_definitions(viper_runtime PRIVATE _POSIX_C_SOURCE=200809L)
endif ()

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
    target_compile_definitions(viper_runtime PRIVATE VIPER_ENABLE_GRAPHICS)
    target_include_directories(viper_runtime PRIVATE
            ${PROJECT_SOURCE_DIR}/src/lib/graphics/include
    )
    target_link_libraries(viper_runtime PUBLIC vipergfx)
else ()
    message(STATUS "viper_runtime: Graphics support disabled (vipergfx not available)")
endif ()
