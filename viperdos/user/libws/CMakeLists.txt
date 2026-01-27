# ViperDOS WebSocket Client Library
# User-space WebSocket (RFC 6455) client implementation

set(LIBWS_SOURCES
        src/websocket.cpp
)

add_library(viperws STATIC ${LIBWS_SOURCES})

target_compile_options(viperws PRIVATE
        -ffreestanding
        -nostdinc
        -fno-stack-protector
        -fno-exceptions
        -fno-rtti
        -fno-threadsafe-statics
        -mcpu=cortex-a72
        -Wall
        -Wextra
        -O2
        -Wno-unused-function
)

target_include_directories(viperws PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../libc/include
)
