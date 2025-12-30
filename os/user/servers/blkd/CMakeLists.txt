# Block device server (blkd)
# User-space VirtIO block device driver server

add_executable(blkd.elf main.cpp)

target_compile_options(blkd.elf PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(blkd.elf PRIVATE ${USER_LINK_OPTIONS})

target_include_directories(blkd.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/user/libc/include
        ${CMAKE_SOURCE_DIR}/user/libc/include/c++
        ${CMAKE_SOURCE_DIR}/user/libvirtio/include
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(blkd.elf PRIVATE viperlibc libvirtio)
