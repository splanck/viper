# SPDX-License-Identifier: GPL-3.0-only
# File: tests/codegen/common/CMakeLists.txt
# Purpose: Tests for shared codegen utilities (architecture-agnostic).

set(_common_tests
        dataflow_liveness
        runtime_manifest)

foreach (_test IN LISTS _common_tests)
    set(_target test_codegen_common_${_test})
    set(_source ${CMAKE_CURRENT_SOURCE_DIR}/test_${_test}.cpp)

    add_executable(${_target} ${_source})
    if (WIN32)
        target_sources(${_target} PRIVATE
                ${VIPER_TESTS_DIR}/common/WinDialogSuppress.c)
    endif ()
    target_link_libraries(${_target} PRIVATE viper_codegen_common)
    add_test(NAME codegen_common_${_test} COMMAND ${_target})
    set_tests_properties(codegen_common_${_test} PROPERTIES LABELS "codegen;unit")
endforeach ()
