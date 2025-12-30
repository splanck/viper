# ViperOS user-space VirtIO library
# Provides VirtIO device access for user-space drivers

set(LIBVIRTIO_SOURCES
    src/virtio.cpp
    src/virtqueue.cpp
    src/blk.cpp
    src/net.cpp
)

# Create static library
add_library(libvirtio STATIC ${LIBVIRTIO_SOURCES})

target_include_directories(libvirtio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/user
    ${CMAKE_SOURCE_DIR}/include
)

# Freestanding compilation - no standard library dependencies
target_compile_options(libvirtio PRIVATE
    -ffreestanding
    -fno-builtin
    -nostdlib
    -Wall
    -Wextra
    -Werror
    -fno-exceptions
    -fno-rtti
)

# Set output directory
set_target_properties(libvirtio PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
