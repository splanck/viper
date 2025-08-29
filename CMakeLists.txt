cmake_minimum_required(VERSION 3.16)
project(ilc LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
