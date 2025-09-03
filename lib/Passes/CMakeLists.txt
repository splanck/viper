add_library(Passes Mem2Reg.cpp)
target_link_libraries(Passes PUBLIC Analysis il_core)
target_include_directories(Passes PUBLIC ${CMAKE_SOURCE_DIR}/lib)
