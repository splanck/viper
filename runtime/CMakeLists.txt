add_library(rt STATIC rt.c)
target_include_directories(rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
