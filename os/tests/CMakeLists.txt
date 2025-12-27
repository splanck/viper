# ViperOS Tests CMakeLists.txt
#
# Test categories:
# 1. Build verification - check that artifacts are built correctly
# 2. QEMU boot tests - run code in emulator and verify output

# Find QEMU
find_program(QEMU_AARCH64
    NAMES qemu-system-aarch64
    PATHS
        /opt/homebrew/opt/qemu/bin
        /usr/local/bin
        /usr/bin
)

if(QEMU_AARCH64)
    message(STATUS "Found QEMU: ${QEMU_AARCH64}")
    set(VIPEROS_HAS_QEMU TRUE)
else()
    message(STATUS "QEMU not found - QEMU tests will be skipped")
    set(VIPEROS_HAS_QEMU FALSE)
endif()

# -----------------------------------------------------------------------------
# Build Verification Tests
# -----------------------------------------------------------------------------

# Test that kernel.elf is a valid AArch64 ELF
add_test(
    NAME build_kernel_elf_valid
    COMMAND ${CMAKE_COMMAND} -E env
        test -f ${CMAKE_BINARY_DIR}/kernel.elf
)
set_tests_properties(build_kernel_elf_valid PROPERTIES
    LABELS "build"
)

# Test that vinit.elf exists
add_test(
    NAME build_vinit_elf_valid
    COMMAND ${CMAKE_COMMAND} -E env
        test -f ${CMAKE_BINARY_DIR}/vinit.elf
)
set_tests_properties(build_vinit_elf_valid PROPERTIES
    LABELS "build"
)

# Test that BOOTAA64.EFI exists
add_test(
    NAME build_bootloader_valid
    COMMAND ${CMAKE_COMMAND} -E env
        test -f ${CMAKE_BINARY_DIR}/BOOTAA64.EFI
)
set_tests_properties(build_bootloader_valid PROPERTIES
    LABELS "build"
)

# -----------------------------------------------------------------------------
# Toolchain Boot Test
# -----------------------------------------------------------------------------

# Build the toolchain test (minimal freestanding C++ that runs in QEMU)
add_executable(toolchain-test.elf
    boot/startup.S
    boot/toolchain-test.cpp
)

target_compile_options(toolchain-test.elf PRIVATE
    -ffreestanding
    -fno-stack-protector
    -fno-exceptions
    -fno-rtti
    -mcpu=cortex-a72
    -mstrict-align
    -Wall
    -Wextra
)

target_link_options(toolchain-test.elf PRIVATE
    -nostdlib
    -static
    -T ${CMAKE_CURRENT_SOURCE_DIR}/boot/toolchain-test.ld
)

# -----------------------------------------------------------------------------
# QEMU Tests (only if QEMU is available)
# -----------------------------------------------------------------------------

if(VIPEROS_HAS_QEMU)
    # Create test runner script
    file(WRITE ${CMAKE_BINARY_DIR}/run_qemu_test.sh
"#!/bin/bash
# QEMU test runner - runs kernel and checks for expected output
KERNEL=\"\$1\"
EXPECTED=\"\$2\"
TIMEOUT=5

OUTPUT=\$(timeout \${TIMEOUT}s ${QEMU_AARCH64} \\
    -machine virt \\
    -cpu cortex-a72 \\
    -m 64M \\
    -nographic \\
    -kernel \"\${KERNEL}\" \\
    -serial mon:stdio \\
    2>&1)

if echo \"\${OUTPUT}\" | grep -q \"\${EXPECTED}\"; then
    echo \"PASS: Found expected output\"
    exit 0
else
    echo \"FAIL: Expected '\${EXPECTED}' not found in output:\"
    echo \"\${OUTPUT}\"
    exit 1
fi
")
    file(CHMOD ${CMAKE_BINARY_DIR}/run_qemu_test.sh
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    )

    # Toolchain test - verify cross-compilation works
    add_test(
        NAME qemu_toolchain_test
        COMMAND ${CMAKE_BINARY_DIR}/run_qemu_test.sh
            ${CMAKE_BINARY_DIR}/toolchain-test.elf
            "Toolchain works!"
    )
    set_tests_properties(qemu_toolchain_test PROPERTIES
        LABELS "qemu;integration"
        TIMEOUT 10
    )

    # Kernel boot test - verify kernel starts and prints banner
    add_test(
        NAME qemu_kernel_boot
        COMMAND ${CMAKE_BINARY_DIR}/run_qemu_test.sh
            ${CMAKE_BINARY_DIR}/kernel.elf
            "ViperOS"
    )
    set_tests_properties(qemu_kernel_boot PROPERTIES
        LABELS "qemu;integration"
        TIMEOUT 15
    )

    # -----------------------------------------------------------------------------
    # Kernel Subsystem Tests (run inside QEMU)
    # -----------------------------------------------------------------------------

    # Create extended test runner with longer timeout for full test suites
    file(WRITE ${CMAKE_BINARY_DIR}/run_qemu_kernel_tests.sh
"#!/bin/bash
# QEMU kernel test runner - runs kernel with disk and checks for test results
KERNEL=\"\$1\"
EXPECTED=\"\$2\"
TIMEOUT=30
BUILD_DIR=\"\$(dirname \"\${KERNEL}\")\"
DISK_IMG=\"\${BUILD_DIR}/disk.img\"

# Build QEMU command
QEMU_OPTS=(
    -machine virt
    -cpu cortex-a72
    -m 128M
    -nographic
    -kernel \"\${KERNEL}\"
    -serial mon:stdio
    -device virtio-rng-device
)

# Add disk if it exists
if [[ -f \"\${DISK_IMG}\" ]]; then
    QEMU_OPTS+=(
        -drive \"file=\${DISK_IMG},if=none,format=raw,id=disk0\"
        -device virtio-blk-device,drive=disk0
    )
fi

OUTPUT=\$(timeout \${TIMEOUT}s ${QEMU_AARCH64} \"\${QEMU_OPTS[@]}\" 2>&1)

if echo \"\${OUTPUT}\" | grep -q \"\${EXPECTED}\"; then
    echo \"PASS: Found expected output: \${EXPECTED}\"
    # Also print test summary if available
    echo \"\${OUTPUT}\" | grep -E '\\[TEST\\]|\\[RESULT\\]|\\[SUITE\\]|Passed:|Failed:'
    exit 0
else
    echo \"FAIL: Expected '\${EXPECTED}' not found in output\"
    echo \"=== Full output ===\"
    echo \"\${OUTPUT}\"
    exit 1
fi
")
    file(CHMOD ${CMAKE_BINARY_DIR}/run_qemu_kernel_tests.sh
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    )

    # Storage subsystem tests (assigns, VFS, ViperFS)
    add_test(
        NAME qemu_storage_tests
        COMMAND ${CMAKE_BINARY_DIR}/run_qemu_kernel_tests.sh
            ${CMAKE_BINARY_DIR}/kernel.elf
            "ALL STORAGE TESTS PASSED"
    )
    set_tests_properties(qemu_storage_tests PROPERTIES
        LABELS "qemu;kernel;storage"
        TIMEOUT 60
    )

    # Viper subsystem tests (channels, poll, capabilities)
    add_test(
        NAME qemu_viper_tests
        COMMAND ${CMAKE_BINARY_DIR}/run_qemu_kernel_tests.sh
            ${CMAKE_BINARY_DIR}/kernel.elf
            "ALL VIPER TESTS PASSED"
    )
    set_tests_properties(qemu_viper_tests PROPERTIES
        LABELS "qemu;kernel;viper"
        TIMEOUT 60
    )

    # IPC ping-pong test
    add_test(
        NAME qemu_ipc_pingpong
        COMMAND ${CMAKE_BINARY_DIR}/run_qemu_kernel_tests.sh
            ${CMAKE_BINARY_DIR}/kernel.elf
            "Ping task done"
    )
    set_tests_properties(qemu_ipc_pingpong PROPERTIES
        LABELS "qemu;kernel;ipc"
        TIMEOUT 60
    )

    # Scheduler test - verify scheduler starts
    add_test(
        NAME qemu_scheduler_start
        COMMAND ${CMAKE_BINARY_DIR}/run_qemu_kernel_tests.sh
            ${CMAKE_BINARY_DIR}/kernel.elf
            "Starting scheduler"
    )
    set_tests_properties(qemu_scheduler_start PROPERTIES
        LABELS "qemu;kernel;scheduler"
        TIMEOUT 60
    )
endif()
