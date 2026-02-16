# Console server (consoled)
# User-space console output server with GUI window

add_executable(consoled.sys
        main.cpp
        ansi.cpp
        keymap.cpp
        request.cpp
        shell.cpp
        text_buffer.cpp
        embedded_shell.cpp
        shell_io.cpp
        shell_cmds.cpp
)

target_compile_options(consoled.sys PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(consoled.sys PRIVATE ${USER_LINK_OPTIONS})

target_include_directories(consoled.sys PRIVATE
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/user/libc/include
        ${CMAKE_SOURCE_DIR}/user/libc/include/c++
        ${CMAKE_SOURCE_DIR}/user/libgui/include
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(consoled.sys PRIVATE viperlibc vipergui)
