# ViperOS Tests CMakeLists.txt
#
# Test categories:
# 1. Host tests - native C tests that run on the build machine
# 2. Build verification - check that artifacts are built correctly
# 3. QEMU integration tests - boot OS in emulator and verify output
#
# Usage:
#   cmake -S . -B build && cmake --build build && ctest --test-dir build -V
#
# Adding new tests:
#   Host test:  add_host_test(test_name source.c)
#   QEMU test:  add_qemu_test(test_name --expect "pattern" [options])

# =============================================================================
# Configuration
# =============================================================================

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

# Test runner script location
set(QEMU_TEST_RUNNER "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_test.sh")

# Log directory for test output
set(TEST_LOG_DIR "${CMAKE_BINARY_DIR}/test-logs")
file(MAKE_DIRECTORY ${TEST_LOG_DIR})

# =============================================================================
# Helper Functions
# =============================================================================

# add_host_test(name source.c [source2.c ...])
#   Adds a host-side unit test compiled with the HOST compiler (not cross-compiler)
#   This works around cross-compilation by invoking the native cc directly
function(add_host_test name)
    # Get source files with full paths
    set(_sources "")
    foreach(_src ${ARGN})
        if(IS_ABSOLUTE ${_src})
            list(APPEND _sources ${_src})
        else()
            list(APPEND _sources ${CMAKE_CURRENT_SOURCE_DIR}/${_src})
        endif()
    endforeach()

    # Output executable location
    set(_output ${CMAKE_BINARY_DIR}/${name})

    # Build with host compiler (cc) at build time
    # Using add_custom_command/target to invoke native compiler
    add_custom_command(
        OUTPUT ${_output}
        COMMAND cc -std=c17 -Wall -Wextra -O2
                -I${CMAKE_CURRENT_SOURCE_DIR}/host
                ${_sources}
                -o ${_output}
        DEPENDS ${_sources}
        COMMENT "Building host test: ${name}"
        VERBATIM
    )

    add_custom_target(${name} ALL DEPENDS ${_output})

    # Register with CTest
    add_test(
        NAME host_${name}
        COMMAND ${_output}
    )
    set_tests_properties(host_${name} PROPERTIES
        LABELS "host"
    )
endfunction()

# add_qemu_test(name [options])
#   Adds a QEMU integration test
#   Options are passed to run_qemu_test.sh
#   Common options:
#     --expect <pattern>   Pattern that must appear (required, repeatable)
#     --forbid <pattern>   Pattern that must NOT appear (repeatable)
#     --timeout <secs>     Timeout in seconds (default: 30)
#     --disk               Use disk image (adds build/disk.img)
function(add_qemu_test name)
    if(NOT VIPEROS_HAS_QEMU)
        return()
    endif()

    # Parse arguments to check for --disk flag
    set(_has_disk FALSE)
    set(_args ${ARGN})
    list(FIND _args "--disk" _disk_idx)
    if(NOT _disk_idx EQUAL -1)
        set(_has_disk TRUE)
        list(REMOVE_AT _args ${_disk_idx})
    endif()

    # Build command
    set(_cmd
        ${QEMU_TEST_RUNNER}
        --qemu ${QEMU_AARCH64}
        --kernel ${CMAKE_BINARY_DIR}/kernel.elf
        --name ${name}
        --log-dir ${TEST_LOG_DIR}
    )

    if(_has_disk)
        list(APPEND _cmd --disk ${CMAKE_BINARY_DIR}/disk.img)
    endif()

    # Append remaining args
    list(APPEND _cmd ${_args})

    add_test(
        NAME qemu_${name}
        COMMAND ${_cmd}
    )
    set_tests_properties(qemu_${name} PROPERTIES
        LABELS "qemu"
        TIMEOUT 90
    )
endfunction()

# =============================================================================
# Host Unit Tests
# =============================================================================

# Sanity test - verifies test framework works
add_host_test(test_sanity host/test_sanity.c)

# Crypto known-answer tests for user/libssh primitives
add_host_test(test_ssh_crypto host/test_ssh_crypto.c ../user/libssh/ssh_crypto.c)

# Syscall table invariant tests (Python-based static analysis)
find_package(Python3 QUIET)
if(Python3_FOUND)
    add_test(
        NAME host_syscall_table_invariants
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/host/test_syscall_table.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(host_syscall_table_invariants PROPERTIES
        LABELS "host;syscall"
    )
endif()

# =============================================================================
# Build Verification Tests
# =============================================================================

# Test that kernel.elf exists
add_test(
    NAME build_kernel_elf_valid
    COMMAND test -f ${CMAKE_BINARY_DIR}/kernel.elf
)
set_tests_properties(build_kernel_elf_valid PROPERTIES LABELS "build")

# Test that vinit.elf exists
add_test(
    NAME build_vinit_elf_valid
    COMMAND test -f ${CMAKE_BINARY_DIR}/vinit.elf
)
set_tests_properties(build_vinit_elf_valid PROPERTIES LABELS "build")

# Test that BOOTAA64.EFI exists
add_test(
    NAME build_bootloader_valid
    COMMAND test -f ${CMAKE_BINARY_DIR}/BOOTAA64.EFI
)
set_tests_properties(build_bootloader_valid PROPERTIES LABELS "build")

# =============================================================================
# Toolchain Verification Test
# =============================================================================

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

# =============================================================================
# QEMU Integration Tests
# =============================================================================

if(VIPEROS_HAS_QEMU)
    # Make test runner executable
    execute_process(
        COMMAND chmod +x ${QEMU_TEST_RUNNER}
    )

    # --- Toolchain test ---
    # Verifies cross-compilation works by running minimal code
    add_test(
        NAME qemu_toolchain
        COMMAND ${QEMU_TEST_RUNNER}
            --qemu ${QEMU_AARCH64}
            --kernel ${CMAKE_BINARY_DIR}/toolchain-test.elf
            --name toolchain
            --log-dir ${TEST_LOG_DIR}
            --timeout 10
            --memory 64M
            --expect "Toolchain works!"
    )
    set_tests_properties(qemu_toolchain PROPERTIES
        LABELS "qemu;toolchain"
        TIMEOUT 30
    )

    # --- Boot smoke test ---
    # Verifies kernel boots and prints banner
    add_qemu_test(boot_smoke
        --timeout 15
        --expect "ViperOS"
        --expect "AArch64"
    )

    # --- Memory initialization test ---
    add_qemu_test(memory_init
        --timeout 20
        --expect "\\[pmm\\].*physical memory"
        --expect "\\[vmm\\].*VMM initialized"
        --expect "\\[kheap\\].*kernel heap"
    )

    # --- Storage subsystem tests ---
    add_qemu_test(storage_tests
        --disk
        --timeout 45
        --expect "ALL STORAGE TESTS PASSED"
        --forbid "FAILED"
    )
    set_tests_properties(qemu_storage_tests PROPERTIES LABELS "qemu;kernel;storage")

    # --- Viper subsystem tests ---
    add_qemu_test(viper_tests
        --disk
        --timeout 45
        --expect "ALL VIPER TESTS PASSED"
        --forbid "FAILED"
    )
    set_tests_properties(qemu_viper_tests PROPERTIES LABELS "qemu;kernel;viper")

    # --- Syscall dispatch tests ---
    add_qemu_test(syscall_dispatch
        --disk
        --timeout 45
        --expect "ALL SYSCALL TESTS PASSED"
        --forbid "FAILED"
    )
    set_tests_properties(qemu_syscall_dispatch PROPERTIES LABELS "qemu;kernel;syscall")

    # --- IPC ping-pong test ---
    add_qemu_test(ipc_pingpong
        --disk
        --timeout 45
        --expect "Ping task done"
    )
    set_tests_properties(qemu_ipc_pingpong PROPERTIES LABELS "qemu;kernel;ipc")

    # --- Scheduler test ---
    add_qemu_test(scheduler
        --disk
        --timeout 45
        --expect "Starting scheduler"
        --expect "First task:"
    )
    set_tests_properties(qemu_scheduler PROPERTIES LABELS "qemu;kernel;scheduler")

    # --- User fault tests ---
    # NOTE: Disabled for now - userfault tests don't run automatically at boot
    # to avoid page fault output during normal interactive use.
    # The fault handling code is still tested implicitly via the test programs
    # on disk (faulttest_null.elf, faulttest_illegal.elf) which can be run manually.
    # TODO: Add kernel command-line flag to enable test mode
    # add_qemu_test(userfault_tests
    #     --disk
    #     --timeout 90
    #     --expect "ALL USERFAULT TESTS PASSED"
    #     --expect "USERFAULT.*kind=permission_fault"
    #     --expect "USERFAULT.*kind=illegal_instruction"
    #     --expect "Kernel still alive after null deref test"
    #     --expect "Kernel still alive after illegal insn test"
    #     --forbid "KERNEL PANIC"
    #     --forbid "USERFAULT TESTS FAILED"
    # )
    # set_tests_properties(qemu_userfault_tests PROPERTIES LABELS "qemu;kernel;userfault")

	    # --- Network initialization test ---
	    add_qemu_test(network_init
	        --disk
	        --timeout 45
	        --expect "Network stack initialized"
	        --expect "virtio-net.*initialized"
	    )
	    set_tests_properties(qemu_network_init PROPERTIES LABELS "qemu;kernel;network")

	    # --- User space test (vinit starts) ---
	    add_qemu_test(userspace_init
	        --disk
	        --timeout 90
	        --expect "Init Process"
	        --expect "ViperOS.*Shell"
	    )
	    set_tests_properties(qemu_userspace_init PROPERTIES LABELS "qemu;kernel;userspace")

	    # --- Microkernel servers: fallback path (no dedicated devices) ---
	    # When the server ELFs exist but no dedicated VirtIO devices are present,
	    # vinit should attempt startup and then fall back to kernel services.
	    add_qemu_test(microkernel_servers_fallback
	        --disk
	        --timeout 120
	        --expect "\\[vinit\\] Starting microkernel servers"
	        --expect "\\[vinit\\] Servers unavailable, using kernel services"
	    )
	    set_tests_properties(qemu_microkernel_servers_fallback PROPERTIES LABELS "qemu;userspace;microkernel")

	    # --- Microkernel servers: positive bring-up path (dedicated devices) ---
	    add_qemu_test(microkernel_servers_ready
	        --disk
	        --microkernel-devices
	        --timeout 180
	        --expect "\\[vinit\\] BLKD: ready"
	        --expect "\\[vinit\\] NETD: ready"
	        --expect "\\[vinit\\] FSD: ready"
	    )
	    set_tests_properties(qemu_microkernel_servers_ready PROPERTIES LABELS "qemu;userspace;microkernel")

	    # --- libc→fsd forwarding smoke test ---
	    # Runs a small user program after fsd is ready that creates a file via libc
	    # (expected to be routed to fsd) and confirms the kernel VFS can't see it.
	    add_qemu_test(libc_fsd_smoke
	        --disk
	        --microkernel-devices
	        --timeout 180
	        --expect "\\[fsd_smoke\\] OK: libc routed to fsd"
	    )
	    set_tests_properties(qemu_libc_fsd_smoke PROPERTIES LABELS "qemu;userspace;microkernel;libc")

	    # --- netd IPC smoke test ---
	    # Runs a small user program after netd is ready that queries NETD for basic
	    # interface configuration and prints a success marker.
	    add_qemu_test(netd_smoke
	        --disk
	        --microkernel-devices
	        --timeout 180
	        --expect "\\[netd_smoke\\] OK: NETD ip="
	    )
	    set_tests_properties(qemu_netd_smoke PROPERTIES LABELS "qemu;userspace;microkernel;network")

	    # --- TLS library smoke test ---
	    # Tests user-space TLS library API (config, session creation, info queries).
	    # Does not require network - tests API without actual connections.
	    add_qemu_test(tls_smoke
	        --disk
	        --microkernel-devices
	        --timeout 180
	        --expect "TLS smoke test PASSED"
	    )
	    set_tests_properties(qemu_tls_smoke PROPERTIES LABELS "qemu;userspace;tls")

	endif()

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "ViperOS Test Configuration:")
message(STATUS "  Host tests:      enabled")
message(STATUS "  Build tests:     enabled")
message(STATUS "  QEMU tests:      ${VIPEROS_HAS_QEMU}")
if(VIPEROS_HAS_QEMU)
    message(STATUS "  QEMU path:       ${QEMU_AARCH64}")
endif()
message(STATUS "  Test logs:       ${TEST_LOG_DIR}")
message(STATUS "")
message(STATUS "Run tests with: ctest --test-dir build -V")
message(STATUS "Run host only:  ctest --test-dir build -L host")
message(STATUS "Run QEMU only:  ctest --test-dir build -L qemu")
message(STATUS "")
