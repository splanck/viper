add_library(rt STATIC rt_memory.c rt_string.c rt_io.c rt_math.c rt_random.c)
target_include_directories(rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rt PUBLIC m)
