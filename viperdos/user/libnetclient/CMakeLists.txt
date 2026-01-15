# ViperDOS user-space network client library
# Provides IPC client helpers for talking to the netd server.

set(LIBNETCLIENT_SOURCES
        src/netclient.cpp
)

add_library(vipernetclient STATIC ${LIBNETCLIENT_SOURCES})

target_include_directories(vipernetclient PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(vipernetclient PRIVATE
        -ffreestanding
        -fno-builtin
        -nostdlib
        -Wall
        -Wextra
        -Werror
        -fno-exceptions
        -fno-rtti
)

set_target_properties(vipernetclient PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

