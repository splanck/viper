# CMake configuration for ViperTUI

cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(tui STATIC src/version.cpp src/term/term_io.cpp src/term/session.cpp)

target_include_directories(tui PUBLIC include)

enable_testing()
add_subdirectory(tests)
