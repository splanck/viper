# ViperDOS TLS Library
# User-space TLS 1.3 client implementation

set(LIBTLS_SOURCES
        src/crypto.c
        src/tls.c
)

add_library(vipertls STATIC ${LIBTLS_SOURCES})

target_compile_options(vipertls PRIVATE
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

target_include_directories(vipertls PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../libc/include
)
