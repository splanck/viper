set(RT_SOURCES
        rt_error.c
        rt_trap.c
        rt_fp.c
        rt_memory.c
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
        rt_array_str.c
        rt_array_obj.c
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
)

set(RT_PUBLIC_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_error.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_trap.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_fp.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_math.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_numeric.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_random.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_string.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_array.h
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
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_modvar.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_args.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rt_ns_bridge.h
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

target_compile_definitions(viper_runtime
        PRIVATE $<$<CONFIG:Debug>:VIPER_RC_DEBUG=1>
)

install(FILES ${RT_PUBLIC_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper_runtime
)

install(FILES ${PROJECT_SOURCE_DIR}/include/viper/runtime/rt.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper/runtime
)
