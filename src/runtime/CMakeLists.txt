set(RT_SOURCES
  rt_error.c
  rt_fp.c
  rt_memory.c
  rt_string_ops.c
  rt_string_format.c
  rt_string_encode.c
  rt_io.c
  rt_file.c
  rt_file_io.c
  rt_file_path.c
  rt_math.c
  rt_random.c
  rt_array.c
  rt_heap.c
  rt_numeric.c
  rt_numeric_conv.c
  rt_debug.c
  rt_format.c
  rt_int_format.c
  rt_term.c
  rt_object.c
)

set(RT_PUBLIC_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_error.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_fp.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_math.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_numeric.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_random.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_string.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_array.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_heap.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_object.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_debug.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_file.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_format.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_int_format.h
)

add_library(viper_runtime STATIC ${RT_SOURCES})
target_include_directories(viper_runtime
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/runtime>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper_runtime>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

if(NOT WIN32)
  target_link_libraries(viper_runtime PUBLIC m)
  target_compile_definitions(viper_runtime PRIVATE _POSIX_C_SOURCE=200809L)
endif()

target_compile_definitions(viper_runtime
  PRIVATE $<$<CONFIG:Debug>:VIPER_RC_DEBUG=1>
)

install(FILES ${RT_PUBLIC_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper_runtime
)
