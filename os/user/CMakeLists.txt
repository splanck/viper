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

# Build the virtio library for user-space drivers
add_subdirectory(libvirtio)

# Build the microkernel servers
add_subdirectory(servers)

# Build the SSH library
add_subdirectory(libssh)

# Build the filesystem client library
add_subdirectory(libfsclient)

# Build the network client library
add_subdirectory(libnetclient)

# Build the TLS library
add_subdirectory(libtls)

# Build the HTTP client library
add_subdirectory(libhttp)

# Helper function to create user programs with libc (.prg extension)
function(add_user_program name)
    add_executable(${name}.prg ${ARGN})
    target_compile_options(${name}.prg PRIVATE ${USER_COMPILE_OPTIONS})
    target_link_options(${name}.prg PRIVATE ${USER_LINK_OPTIONS})
    target_include_directories(${name}.prg PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include/c++
    )
    target_link_libraries(${name}.prg PRIVATE viperlibc)
endfunction()

# Helper function to create system binaries (.sys extension)
function(add_system_binary name)
    add_executable(${name}.sys ${ARGN})
    target_compile_options(${name}.sys PRIVATE ${USER_COMPILE_OPTIONS})
    target_link_options(${name}.sys PRIVATE ${USER_LINK_OPTIONS})
    target_include_directories(${name}.sys PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include/c++
    )
    target_link_libraries(${name}.sys PRIVATE viperlibc)
endfunction()

# vinit - init process (multi-file) - system binary
add_system_binary(vinit
        vinit/vinit.cpp
        vinit/io.cpp
        vinit/readline.cpp
        vinit/cmd_system.cpp
        vinit/cmd_fs.cpp
        vinit/cmd_misc.cpp
        vinit/shell.cpp
)
# Note: viperlibc must come last to resolve symbols from other libs (e.g., vipertls uses gethostbyname)
target_link_libraries(vinit.sys PRIVATE viperfsclient vipernetclient vipertls viperlibc)

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

# ping - ICMP ping utility
add_user_program(ping ping/ping.cpp)

# devices - hardware device listing utility
add_user_program(devices devices/devices.cpp)

# edit - simple nano-like text editor
add_user_program(edit edit/edit.cpp)

# fsd_smoke - validates libc→fsd routing when fsd is present
add_user_program(fsd_smoke fstest/fsd_smoke.c)

# netd_smoke - validates that NETD responds to IPC requests when present
add_user_program(netd_smoke fstest/netd_smoke.cpp)

# tls_smoke - validates user-space TLS library
add_executable(tls_smoke.prg fstest/tls_smoke.c)
target_compile_options(tls_smoke.prg PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(tls_smoke.prg PRIVATE ${USER_LINK_OPTIONS})
target_include_directories(tls_smoke.prg PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libtls/include
)
target_link_libraries(tls_smoke.prg PRIVATE vipertls viperlibc)

# Helper function for SSH-enabled programs (.prg extension)
function(add_ssh_program name)
    add_executable(${name}.prg ${ARGN})
    target_compile_options(${name}.prg PRIVATE ${USER_COMPILE_OPTIONS})
    target_link_options(${name}.prg PRIVATE ${USER_LINK_OPTIONS})
    target_include_directories(${name}.prg PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include/c++
            ${CMAKE_CURRENT_SOURCE_DIR}/libssh/include
    )
    target_link_libraries(${name}.prg PRIVATE viperssh viperlibc)
endfunction()

# ssh - SSH client
add_ssh_program(ssh ssh/ssh.c)

# sftp - SFTP client
add_ssh_program(sftp sftp/sftp.c)
