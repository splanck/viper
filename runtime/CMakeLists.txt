add_library(rt STATIC rt_memory.c rt_string.c rt_io.c rt_math.c rt_random.c)
target_include_directories(rt PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/viper>
)

if(NOT WIN32)
  target_link_libraries(rt PUBLIC m)
endif()

install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/rt.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_math.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rt_random.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper
)
