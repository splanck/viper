# ViperDOS Userspace Programs

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

# Build the GUI servers (networking is now in-kernel)
add_subdirectory(servers)

# Build the SSH library
add_subdirectory(libssh)

# Build the TLS library
add_subdirectory(libtls)

# Build the HTTP client library
add_subdirectory(libhttp)

# Build the GUI client library
add_subdirectory(libgui)

# Build the Widget toolkit library
add_subdirectory(libwidget)

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
target_link_libraries(vinit.sys PRIVATE vipertls viperlibc)

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

# Helper function for GUI programs (.prg extension)
function(add_gui_program name)
    add_executable(${name}.prg ${ARGN})
    target_compile_options(${name}.prg PRIVATE ${USER_COMPILE_OPTIONS})
    target_link_options(${name}.prg PRIVATE ${USER_LINK_OPTIONS})
    target_include_directories(${name}.prg PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include/c++
            ${CMAKE_CURRENT_SOURCE_DIR}/libgui/include
    )
    target_link_libraries(${name}.prg PRIVATE vipergui viperlibc)
endfunction()

# Helper function for Widget-based GUI programs (.prg extension)
function(add_widget_program name)
    add_executable(${name}.prg ${ARGN})
    target_compile_options(${name}.prg PRIVATE ${USER_COMPILE_OPTIONS})
    target_link_options(${name}.prg PRIVATE ${USER_LINK_OPTIONS})
    target_include_directories(${name}.prg PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/include/c++
            ${CMAKE_CURRENT_SOURCE_DIR}/libgui/include
            ${CMAKE_CURRENT_SOURCE_DIR}/libwidget/include
    )
    target_link_libraries(${name}.prg PRIVATE viperwidget vipergui viperlibc)
endfunction()

# hello_gui - GUI test program
add_gui_program(hello_gui hello_gui/hello_gui.c)

# taskbar - Desktop shell taskbar
add_gui_program(taskbar taskbar/taskbar.c)

# workbench - Amiga-inspired desktop environment (system binary)
add_executable(workbench.sys
        workbench/src/main.cpp
        workbench/src/desktop.cpp
        workbench/src/filebrowser.cpp
        workbench/src/icons.cpp
)
target_compile_options(workbench.sys PRIVATE ${USER_COMPILE_OPTIONS})
target_link_options(workbench.sys PRIVATE ${USER_LINK_OPTIONS})
target_include_directories(workbench.sys PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include/c++
        ${CMAKE_CURRENT_SOURCE_DIR}/libgui/include
        ${CMAKE_CURRENT_SOURCE_DIR}/workbench/include
)
target_link_libraries(workbench.sys PRIVATE vipergui viperlibc)

# Phase 3: System Utilities (GUI versions)

# guisysinfo - GUI system information utility
add_gui_program(guisysinfo guisysinfo/main.cpp)
target_include_directories(guisysinfo.prg PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# taskman - GUI task manager
add_gui_program(taskman taskman/main.cpp)
target_include_directories(taskman.prg PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# prefs - GUI preferences application (Amiga-style)
add_gui_program(prefs prefs/main.cpp)
target_include_directories(prefs.prg PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Phase 5: Desktop Applications

# calc - Calculator application
add_subdirectory(calc)

# vedit - Text editor
add_subdirectory(vedit)

# clock - Analog/digital clock
add_subdirectory(clock)

# viewer - Image viewer
add_subdirectory(viewer)
