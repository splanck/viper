# ViperDOS HTTP Client Library
# User-space HTTP/HTTPS client implementation

set(LIBHTTP_SOURCES
        src/http.c
)

add_library(viperhttp STATIC ${LIBHTTP_SOURCES})

target_compile_options(viperhttp PRIVATE
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

target_include_directories(viperhttp PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../libc/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../libtls/include
)

target_link_libraries(viperhttp PRIVATE vipertls)
