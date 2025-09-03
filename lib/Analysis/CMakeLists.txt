add_library(Analysis STATIC CFG.cpp)
target_include_directories(Analysis PUBLIC ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(Analysis PRIVATE il_core)
