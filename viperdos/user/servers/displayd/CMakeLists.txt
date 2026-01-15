# Display server (displayd)
# Provides window management and compositing

add_executable(displayd.sys main.cpp)

target_compile_options(displayd.sys PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(displayd.sys PRIVATE ${USER_LINK_OPTIONS})

target_include_directories(displayd.sys PRIVATE
        ${CMAKE_SOURCE_DIR}/user
        ${CMAKE_SOURCE_DIR}/user/libc/include
        ${CMAKE_SOURCE_DIR}/user/libc/include/c++
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(displayd.sys PRIVATE viperlibc)
