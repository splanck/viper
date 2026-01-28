cmake_minimum_required(VERSION 3.16)
project(ViperDOS VERSION 0.1.0 LANGUAGES C CXX ASM)

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
# Read user programs list from single source of truth
# -----------------------------------------------------------------------------
file(STRINGS "${CMAKE_SOURCE_DIR}/user/programs.txt" USER_PROGRAMS_RAW)
set(USER_PROGRAMS "")
foreach (line ${USER_PROGRAMS_RAW})
    # Skip comments and empty lines
    string(REGEX MATCH "^#" is_comment "${line}")
    string(STRIP "${line}" line_stripped)
    if (NOT is_comment AND NOT "${line_stripped}" STREQUAL "")
        list(APPEND USER_PROGRAMS "${line_stripped}")
    endif ()
endforeach ()

# Build the --add arguments and DEPENDS list for user.img
set(USER_ADD_ARGS "")
set(USER_DEPENDS "")
foreach (prg ${USER_PROGRAMS})
    list(APPEND USER_ADD_ARGS "--add" "${CMAKE_BINARY_DIR}/${prg}.prg:c/${prg}.prg")
    list(APPEND USER_DEPENDS "${prg}.prg")
endforeach ()

# -----------------------------------------------------------------------------
# sys.img - System disk (2MB)
# -----------------------------------------------------------------------------
# Contains system files accessible via kernel VFS at /sys/*
add_custom_command(
        OUTPUT ${SYS_IMAGE}
        COMMAND ${CMAKE_COMMAND} -E echo "Creating sys.img (system disk)..."
        COMMAND ${MKFS_TOOL} ${SYS_IMAGE} 2
        --add ${CMAKE_BINARY_DIR}/vinit.sys:vinit.sys
        --add ${CMAKE_BINARY_DIR}/consoled.sys:consoled.sys
        --add ${CMAKE_BINARY_DIR}/displayd.sys:displayd.sys
        --add ${CMAKE_BINARY_DIR}/workbench.sys:workbench.sys
        --add ${CMAKE_BINARY_DIR}/hello_gui.prg:hello_gui.prg
        DEPENDS
        vinit.sys
        consoled.sys displayd.sys workbench.sys hello_gui.prg
        COMMENT "Creating sys.img with system servers"
        VERBATIM
)
add_custom_target(sys_image ALL DEPENDS ${SYS_IMAGE})

# -----------------------------------------------------------------------------
# user.img - User disk (8MB)
# -----------------------------------------------------------------------------
# Contains user files accessible via fsd
# Programs are read from user/programs.txt (single source of truth)
add_custom_command(
        OUTPUT ${USER_IMAGE}
        COMMAND ${CMAKE_COMMAND} -E echo "Creating user.img (user disk)..."
        COMMAND ${MKFS_TOOL} ${USER_IMAGE} 8
        --mkdir c --mkdir certs --mkdir s --mkdir t
        ${USER_ADD_ARGS}
        DEPENDS ${USER_DEPENDS}
        COMMENT "Creating user.img with user programs"
        VERBATIM
)
add_custom_target(user_image ALL DEPENDS ${USER_IMAGE})

# -----------------------------------------------------------------------------
# Legacy compatibility: disk.img symlink to sys.img
# -----------------------------------------------------------------------------
set(DISK_IMAGE "${CMAKE_BINARY_DIR}/disk.img")
add_custom_command(
        OUTPUT ${DISK_IMAGE}
        COMMAND ${CMAKE_COMMAND} -E rm -f ${DISK_IMAGE}
        COMMAND ${CMAKE_COMMAND} -E create_symlink sys.img ${DISK_IMAGE}
        DEPENDS ${SYS_IMAGE}
        COMMENT "Creating disk.img symlink to sys.img"
        VERBATIM
)
add_custom_target(disk_image ALL DEPENDS ${DISK_IMAGE})

# Testing
enable_testing()
add_subdirectory(tests)
