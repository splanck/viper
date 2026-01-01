# Console server (consoled)
# User-space console output server

add_executable(consoled.elf
        main.cpp
)

target_compile_options(consoled.elf PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(consoled.elf PRIVATE ${USER_LINK_OPTIONS})

target_include_directories(consoled.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/user/libc/include
        ${CMAKE_SOURCE_DIR}/user/libc/include/c++
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(consoled.elf PRIVATE viperlibc)
