# Filesystem server (fsd)
# User-space ViperFS filesystem driver server

add_executable(fsd.sys
        main.cpp
        viperfs.cpp
)

target_compile_options(fsd.sys PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(fsd.sys PRIVATE ${USER_LINK_OPTIONS})

target_include_directories(fsd.sys PRIVATE
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/user/libc/include
        ${CMAKE_SOURCE_DIR}/user/libc/include/c++
        ${CMAKE_SOURCE_DIR}/user/servers/blkd  # For blk_protocol.hpp
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(fsd.sys PRIVATE viperlibc)
