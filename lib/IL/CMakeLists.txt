add_library(IL Utils.cpp)
target_link_libraries(IL PUBLIC il_core)
target_include_directories(IL PUBLIC ${CMAKE_SOURCE_DIR}/lib)
