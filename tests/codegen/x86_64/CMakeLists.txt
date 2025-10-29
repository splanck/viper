# SPDX-License-Identifier: MIT
# File: tests/codegen/x86_64/CMakeLists.txt
# Purpose: Build the x86-64 codegen smoke tests that exercise the adapter
#          ILModule facade and verify minimal assembly output.

set(_viper_codegen_x86_64_tests
        abi_probe
        add_ret
        diff_vm_native
        div_trap
        udiv_urem
        i1_normalization
        pro_epi
        pipeline_equivalence
        f64_const
        return_values
        select
        select_cmov
        shifts
        string_literal
        regalloc_consistency)

list(APPEND _viper_codegen_x86_64_tests
        e2e_bitwise_cli
        e2e_fdiv_cli)

set(_viper_codegen_x86_64_has_gtest OFF)
if (TARGET GTest::gtest_main OR TARGET GTest::gtest OR TARGET gtest_main OR TARGET gtest)
    set(_viper_codegen_x86_64_has_gtest ON)
endif ()

foreach (_viper_codegen_case IN LISTS _viper_codegen_x86_64_tests)
    set(_viper_codegen_target test_codegen_x86_64_${_viper_codegen_case})
    set(_viper_codegen_source
            ${VIPER_TESTS_DIR}/codegen/x86_64/test_${_viper_codegen_case}.cpp)

    if (_viper_codegen_x86_64_has_gtest)
        viper_add_test(
                ${_viper_codegen_target}
                ${_viper_codegen_source}
                TEST_NAME codegen_x86_64_${_viper_codegen_case})
    else ()
        add_executable(${_viper_codegen_target} ${_viper_codegen_source})
        target_link_libraries(${_viper_codegen_target} PRIVATE viper_testing)
        add_test(
                NAME codegen_x86_64_${_viper_codegen_case}
                COMMAND ${_viper_codegen_target})
    endif ()

    target_link_libraries(${_viper_codegen_target} PRIVATE viper_codegen_x86_64 viper_test_common)
endforeach ()
