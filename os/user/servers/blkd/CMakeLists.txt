# Block device server (blkd)
# User-space VirtIO block device driver server

add_executable(blkd.sys main.cpp)

target_compile_options(blkd.sys PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(blkd.sys PRIVATE ${USER_LINK_OPTIONS})

target_include_directories(blkd.sys PRIVATE
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/user/libc/include
        ${CMAKE_SOURCE_DIR}/user/libc/include/c++
        ${CMAKE_SOURCE_DIR}/user/libvirtio/include
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(blkd.sys PRIVATE viperlibc libvirtio)
