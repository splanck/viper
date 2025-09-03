add_library(Analysis CFG.cpp Dominators.cpp)
target_link_libraries(Analysis PRIVATE il_core)
target_include_directories(Analysis PUBLIC ${CMAKE_SOURCE_DIR}/lib)
