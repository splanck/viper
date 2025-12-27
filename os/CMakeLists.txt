cmake_minimum_required(VERSION 3.16)
project(ViperOS VERSION 0.1.0 LANGUAGES C CXX ASM)

# Language standards
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Build vboot (UEFI bootloader)
add_subdirectory(vboot)

# Build kernel
add_subdirectory(kernel)

# Build userspace programs
add_subdirectory(user)

# Testing
enable_testing()
add_subdirectory(tests)

