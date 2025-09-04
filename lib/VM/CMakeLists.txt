add_library(VMTrace Trace.cpp)
target_link_libraries(VMTrace PUBLIC il_core rt support)
target_include_directories(VMTrace PUBLIC ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/src)
