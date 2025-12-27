# ViperOS Userspace Programs

# Compiler flags for freestanding userspace
set(USER_COMPILE_OPTIONS
        -ffreestanding
        -fno-stack-protector
        -fno-exceptions
        -fno-rtti
        -fno-threadsafe-statics
        -fno-use-cxa-atexit
        -mcpu=cortex-a72
        -Wall
        -Wextra
        -O2
)

set(USER_LINK_OPTIONS
        -nostdlib
        -static
        -T ${CMAKE_CURRENT_SOURCE_DIR}/user.ld
)

# Build the libc first
add_subdirectory(libc)

# vinit - init process
add_executable(vinit.elf vinit/vinit.cpp)
target_compile_options(vinit.elf PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(vinit.elf PRIVATE ${USER_LINK_OPTIONS})
target_include_directories(vinit.elf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# hello - simple test program for spawn testing
add_executable(hello.elf hello/hello.cpp)
target_compile_options(hello.elf PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(hello.elf PRIVATE ${USER_LINK_OPTIONS})
target_include_directories(hello.elf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
