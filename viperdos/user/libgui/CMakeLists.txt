# ViperDOS user-space GUI client library
# Provides IPC client helpers for talking to the displayd server.

set(LIBGUI_SOURCES
        src/gui.cpp
)

add_library(vipergui STATIC ${LIBGUI_SOURCES})

target_include_directories(vipergui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/user/servers/displayd
        ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(vipergui PRIVATE
        -ffreestanding
        -fno-builtin
        -nostdlib
        -Wall
        -Wextra
        -Werror
        -fno-exceptions
        -fno-rtti
)

set_target_properties(vipergui PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
