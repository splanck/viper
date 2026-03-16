# SPDX-License-Identifier: GPL-3.0-only
# File: tests/codegen/binenc/CMakeLists.txt
# Purpose: Build unit tests for the binary encoder (x86_64, AArch64).
#          These are pure encoding tests — no execution of generated code —
#          so they run on any host architecture.

# --- x86_64 binary encoder tests ---
set(_x64_tests
        x64_binary_encoder)

foreach (_case IN LISTS _x64_tests)
    set(_target test_binenc_${_case})
    set(_source ${VIPER_TESTS_DIR}/codegen/binenc/test_${_case}.cpp)

    add_executable(${_target} ${_source})
    if (WIN32)
        target_sources(${_target} PRIVATE
                ${VIPER_TESTS_DIR}/common/WinDialogSuppress.c)
    endif ()
    target_link_libraries(${_target} PRIVATE viper_testing viper_codegen_x86_64 viper_codegen_common)
    target_include_directories(${_target} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    viper_add_ctest(${_target} $<TARGET_FILE:${_target}>)
endforeach ()

# --- AArch64 binary encoder tests ---
set(_a64_tests
        a64_binary_encoder)

foreach (_case IN LISTS _a64_tests)
    set(_target test_binenc_${_case})
    set(_source ${VIPER_TESTS_DIR}/codegen/binenc/test_${_case}.cpp)

    add_executable(${_target} ${_source})
    if (WIN32)
        target_sources(${_target} PRIVATE
                ${VIPER_TESTS_DIR}/common/WinDialogSuppress.c)
    endif ()
    target_link_libraries(${_target} PRIVATE viper_testing il_codegen_aarch64 viper_codegen_common)
    target_include_directories(${_target} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    viper_add_ctest(${_target} $<TARGET_FILE:${_target}>)
endforeach ()
