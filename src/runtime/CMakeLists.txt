set(RT_SOURCES
  rt_memory.c
  rt_string.c
  rt_io.c
  rt_math.c
  rt_random.c
  rt_array.c
  rt_heap.c
)

set(RT_PUBLIC_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/rt.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_math.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_random.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_array.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_heap.h
)

add_library(rt STATIC ${RT_SOURCES})
target_include_directories(rt
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/runtime>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

if(NOT WIN32)
  target_link_libraries(rt PUBLIC m)
endif()

install(FILES ${RT_PUBLIC_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper
)
