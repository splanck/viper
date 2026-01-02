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
# Disk Image Creation
# =============================================================================
# Create disk.img for QEMU tests using a shell script wrapper.
# The disk contains:
# - vinit.sys at root (required by kernel loader)
# - All user programs and servers in /c/ directory
# - Standard directories: /c/, /t/, /s/

set(MKFS_TOOL "${CMAKE_SOURCE_DIR}/tools/mkfs.viperfs")
set(DISK_IMAGE "${CMAKE_BINARY_DIR}/disk.img")
set(MICROKERNEL_IMAGE "${CMAKE_BINARY_DIR}/microkernel.img")

# Use a custom command that builds the disk at build time
add_custom_command(
    OUTPUT ${DISK_IMAGE} ${MICROKERNEL_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E echo "Creating ViperFS disk image..."
    COMMAND ${MKFS_TOOL} ${DISK_IMAGE} 8
        --add ${CMAKE_BINARY_DIR}/vinit.sys:vinit.sys
        --mkdir c --mkdir t --mkdir s
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
        --add ${CMAKE_BINARY_DIR}/blkd.sys:c/blkd.sys
        --add ${CMAKE_BINARY_DIR}/netd.sys:c/netd.sys
        --add ${CMAKE_BINARY_DIR}/fsd.sys:c/fsd.sys
        --add ${CMAKE_BINARY_DIR}/consoled.sys:c/consoled.sys
        --add ${CMAKE_BINARY_DIR}/inputd.sys:c/inputd.sys
    COMMAND ${CMAKE_COMMAND} -E copy ${DISK_IMAGE} ${MICROKERNEL_IMAGE}
    DEPENDS
        vinit.sys
        hello.prg fsd_smoke.prg netd_smoke.prg tls_smoke.prg edit.prg
        sftp.prg ssh.prg ping.prg fsinfo.prg netstat.prg sysinfo.prg
        devices.prg mathtest.prg faulttest_null.prg faulttest_illegal.prg
        blkd.sys netd.sys fsd.sys consoled.sys inputd.sys
    COMMENT "Creating ViperFS disk image with all user programs"
    VERBATIM
)

add_custom_target(disk_image ALL DEPENDS ${DISK_IMAGE} ${MICROKERNEL_IMAGE})

# Testing
enable_testing()
add_subdirectory(tests)

