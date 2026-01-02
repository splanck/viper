# Network server (netd)
# User-space TCP/IP network stack server

add_executable(netd.sys
        main.cpp
        netstack.cpp
)

target_compile_options(netd.sys PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(netd.sys PRIVATE ${USER_LINK_OPTIONS})

target_include_directories(netd.sys PRIVATE
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/user/libc/include
        ${CMAKE_SOURCE_DIR}/user/libc/include/c++
        ${CMAKE_SOURCE_DIR}/user/libvirtio/include
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(netd.sys PRIVATE viperlibc libvirtio)
