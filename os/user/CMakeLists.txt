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

# Helper function to create user programs with libc
function(add_user_program name)
    add_executable(${name}.elf ${ARGN})
    target_compile_options(${name}.elf PRIVATE ${USER_COMPILE_OPTIONS})
    target_link_options(${name}.elf PRIVATE ${USER_LINK_OPTIONS})
    target_include_directories(${name}.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include/c++
    )
    target_link_libraries(${name}.elf PRIVATE viperlibc)
endfunction()

# vinit - init process
add_user_program(vinit vinit/vinit.cpp)

# hello - simple test program for spawn testing
add_user_program(hello hello/hello.cpp)

# fsinfo - filesystem information utility
add_user_program(fsinfo fsinfo/fsinfo.cpp)

# netstat - network statistics utility
add_user_program(netstat netstat/netstat.cpp)

# sysinfo - system information and runtime test utility
add_user_program(sysinfo sysinfo/sysinfo.cpp)

# faulttest_null - tests null pointer dereference handling
add_user_program(faulttest_null faulttest/faulttest_null.cpp)

# faulttest_illegal - tests illegal instruction handling
add_user_program(faulttest_illegal faulttest/faulttest_illegal.cpp)

# mathtest - math library test program
add_user_program(mathtest mathtest/mathtest.cpp)
