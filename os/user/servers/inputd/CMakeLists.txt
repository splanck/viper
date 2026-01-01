# Input server (inputd)
# User-space keyboard/mouse input server

add_executable(inputd.elf
        main.cpp
)

target_compile_options(inputd.elf PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(inputd.elf PRIVATE ${USER_LINK_OPTIONS})

target_include_directories(inputd.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/user/libc/include
        ${CMAKE_SOURCE_DIR}/user/libc/include/c++
        ${CMAKE_SOURCE_DIR}/user/libvirtio/include
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(inputd.elf PRIVATE viperlibc libvirtio)
