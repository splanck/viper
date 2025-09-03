add_library(rt STATIC rt.c rt_math.c)
target_include_directories(rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rt PUBLIC m)
