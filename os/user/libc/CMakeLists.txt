# ViperOS minimal libc
# Provides basic C runtime functions for user-space programs

set(LIBC_C_SOURCES
    src/string.c
    src/stdlib.c
    src/stdio.c
    src/ctype.c
    src/unistd.c
    src/errno.c
    src/time.c
    src/syscall.S
)

set(LIBC_CXX_SOURCES
    src/new.cpp
)

# Create static library
add_library(viperlibc STATIC ${LIBC_C_SOURCES} ${LIBC_CXX_SOURCES})

target_include_directories(viperlibc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/c++
    ${CMAKE_SOURCE_DIR}/include
)

# Freestanding compilation - no standard library dependencies
target_compile_options(viperlibc PRIVATE
    -ffreestanding
    -fno-builtin
    -nostdlib
    -Wall
    -Wextra
)

# Set output directory
set_target_properties(viperlibc PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
