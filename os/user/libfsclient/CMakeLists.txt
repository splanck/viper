# ViperOS user-space filesystem client library
# Provides IPC client helpers for talking to the fsd server.

set(LIBFSCLIENT_SOURCES
        src/fsclient.cpp
)

add_library(viperfsclient STATIC ${LIBFSCLIENT_SOURCES})

target_include_directories(viperfsclient PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(viperfsclient PRIVATE
        -ffreestanding
        -fno-builtin
        -nostdlib
        -Wall
        -Wextra
        -Werror
        -fno-exceptions
        -fno-rtti
)

set_target_properties(viperfsclient PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

