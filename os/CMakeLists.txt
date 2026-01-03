cmake_minimum_required(VERSION 3.16)
project(ViperOS VERSION 0.1.0 LANGUAGES C CXX ASM)

# Language standards
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Build vboot (UEFI bootloader)
add_subdirectory(vboot)

# Build kernel
add_subdirectory(kernel)

# Build userspace programs
add_subdirectory(user)

# =============================================================================
# Dual Disk Image Architecture
# =============================================================================
# Two-disk architecture for clean kernel/userspace separation:
#   - sys.img (disk0): System disk - kernel access via VirtIO-blk
#     Contains vinit.sys and all system servers at root level
#   - user.img (disk1): User disk - blkd/fsd access
#     Contains user programs in /c/, certificates in /certs/, etc.
#
# This allows the system to boot to a functional shell even if the user
# disk is missing (graceful degradation to "system-only" mode).

set(MKFS_TOOL "${CMAKE_SOURCE_DIR}/tools/mkfs.viperfs")
set(SYS_IMAGE "${CMAKE_BINARY_DIR}/sys.img")
set(USER_IMAGE "${CMAKE_BINARY_DIR}/user.img")

# -----------------------------------------------------------------------------
# sys.img - System disk (2MB)
# -----------------------------------------------------------------------------
# Contains system files accessible via kernel VFS at /sys/*
add_custom_command(
    OUTPUT ${SYS_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E echo "Creating sys.img (system disk)..."
    COMMAND ${MKFS_TOOL} ${SYS_IMAGE} 2
        --add ${CMAKE_BINARY_DIR}/vinit.sys:vinit.sys
        --add ${CMAKE_BINARY_DIR}/blkd.sys:blkd.sys
        --add ${CMAKE_BINARY_DIR}/netd.sys:netd.sys
        --add ${CMAKE_BINARY_DIR}/fsd.sys:fsd.sys
        --add ${CMAKE_BINARY_DIR}/consoled.sys:consoled.sys
        --add ${CMAKE_BINARY_DIR}/inputd.sys:inputd.sys
        --add ${CMAKE_BINARY_DIR}/displayd.sys:displayd.sys
    DEPENDS
        vinit.sys blkd.sys netd.sys fsd.sys
        consoled.sys inputd.sys displayd.sys
    COMMENT "Creating sys.img with system servers"
    VERBATIM
)
add_custom_target(sys_image ALL DEPENDS ${SYS_IMAGE})

# -----------------------------------------------------------------------------
# user.img - User disk (8MB)
# -----------------------------------------------------------------------------
# Contains user files accessible via fsd
add_custom_command(
    OUTPUT ${USER_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E echo "Creating user.img (user disk)..."
    COMMAND ${MKFS_TOOL} ${USER_IMAGE} 8
        --mkdir c --mkdir certs --mkdir s --mkdir t
        --add ${CMAKE_BINARY_DIR}/hello.prg:c/hello.prg
        --add ${CMAKE_BINARY_DIR}/fsd_smoke.prg:c/fsd_smoke.prg
        --add ${CMAKE_BINARY_DIR}/netd_smoke.prg:c/netd_smoke.prg
        --add ${CMAKE_BINARY_DIR}/tls_smoke.prg:c/tls_smoke.prg
        --add ${CMAKE_BINARY_DIR}/edit.prg:c/edit.prg
        --add ${CMAKE_BINARY_DIR}/sftp.prg:c/sftp.prg
        --add ${CMAKE_BINARY_DIR}/ssh.prg:c/ssh.prg
        --add ${CMAKE_BINARY_DIR}/ping.prg:c/ping.prg
        --add ${CMAKE_BINARY_DIR}/fsinfo.prg:c/fsinfo.prg
        --add ${CMAKE_BINARY_DIR}/netstat.prg:c/netstat.prg
        --add ${CMAKE_BINARY_DIR}/sysinfo.prg:c/sysinfo.prg
        --add ${CMAKE_BINARY_DIR}/devices.prg:c/devices.prg
        --add ${CMAKE_BINARY_DIR}/mathtest.prg:c/mathtest.prg
        --add ${CMAKE_BINARY_DIR}/faulttest_null.prg:c/faulttest_null.prg
        --add ${CMAKE_BINARY_DIR}/faulttest_illegal.prg:c/faulttest_illegal.prg
        --add ${CMAKE_BINARY_DIR}/hello_gui.prg:c/hello_gui.prg
        --add ${CMAKE_BINARY_DIR}/taskbar.prg:c/taskbar.prg
    DEPENDS
        hello.prg fsd_smoke.prg netd_smoke.prg tls_smoke.prg edit.prg
        sftp.prg ssh.prg ping.prg fsinfo.prg netstat.prg sysinfo.prg
        devices.prg mathtest.prg faulttest_null.prg faulttest_illegal.prg
        hello_gui.prg taskbar.prg
    COMMENT "Creating user.img with user programs"
    VERBATIM
)
add_custom_target(user_image ALL DEPENDS ${USER_IMAGE})

# -----------------------------------------------------------------------------
# Legacy compatibility: disk.img alias for sys.img
# -----------------------------------------------------------------------------
set(DISK_IMAGE "${CMAKE_BINARY_DIR}/disk.img")
add_custom_command(
    OUTPUT ${DISK_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E copy ${SYS_IMAGE} ${DISK_IMAGE}
    DEPENDS ${SYS_IMAGE}
    COMMENT "Creating disk.img (legacy alias for sys.img)"
    VERBATIM
)
add_custom_target(disk_image ALL DEPENDS ${DISK_IMAGE})

# Testing
enable_testing()
add_subdirectory(tests)

