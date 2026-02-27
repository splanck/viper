# SPDX-License-Identifier: GPL-3.0-only
# File: tests/conformance/CMakeLists.txt
# Purpose: Register arithmetic conformance test suite.

set(_VIPER_CONFORMANCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ── Shift conformance (AShr, LShr) ─────────────────────────────────────────
viper_add_test(test_shift_conformance
        ${_VIPER_CONFORMANCE_DIR}/ShiftConformanceTests.cpp)
target_link_libraries(test_shift_conformance PRIVATE
        il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB} viper_test_common)
viper_add_ctest(test_shift_conformance test_shift_conformance)

# ── Sub-width arithmetic (I32/I16 checked ops) ────────────────────────────
viper_add_test(test_subwidth_arith
        ${_VIPER_CONFORMANCE_DIR}/SubWidthArithTests.cpp)
target_link_libraries(test_subwidth_arith PRIVATE
        il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB} viper_test_common)
viper_add_ctest(test_subwidth_arith test_subwidth_arith)

# ── Zia arithmetic conformance (opcode verification) ─────────────────────
viper_add_test(test_zia_arith_conformance
        ${_VIPER_CONFORMANCE_DIR}/ZiaArithConformanceTests.cpp)
target_link_libraries(test_zia_arith_conformance PRIVATE ${VIPER_ZIA_LIBS})
viper_add_ctest(test_zia_arith_conformance test_zia_arith_conformance)

# ── Cross-layer deterministic equivalence (VM vs ARM64 native) ───────────
if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    _aarch64_add_test_common(test_crosslayer_arith
            ${_VIPER_CONFORMANCE_DIR}/CrossLayerArithTests.cpp
            "il_codegen_aarch64;viper_cmd_arm64")
    set_tests_properties(test_crosslayer_arith PROPERTIES
            RUN_SERIAL TRUE TIMEOUT 120 LABELS "slow;conformance")
else ()
    # On non-ARM64: build VM-only tests (native tests skip at runtime)
    viper_add_test(test_crosslayer_arith
            ${_VIPER_CONFORMANCE_DIR}/CrossLayerArithTests.cpp)
    target_link_libraries(test_crosslayer_arith PRIVATE
            il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB} viper_test_common)
    viper_add_ctest(test_crosslayer_arith test_crosslayer_arith)
endif ()

# ── BASIC arithmetic golden tests ────────────────────────────────────────
set(BASIC_ILC $<TARGET_FILE:viper>)
set(_BASIC_ARITH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../golden/basic_arith)

set(BASIC_ARITH_TESTS
        intdiv_neg
        intdiv_both_neg
        mod_neg
        mod_both_neg
        mod_pos_neg
        float_div
)

foreach (test_name IN LISTS BASIC_ARITH_TESTS)
    set(_bas_file ${_BASIC_ARITH_DIR}/${test_name}.bas)
    set(_stdout_file ${_BASIC_ARITH_DIR}/${test_name}.stdout)
    if (EXISTS "${_bas_file}" AND EXISTS "${_stdout_file}")
        file(READ ${_stdout_file} _expect_output)
        string(STRIP "${_expect_output}" _expect_output)
        viper_add_ctest(basic_arith_${test_name}
                ${CMAKE_COMMAND}
                -DILC=${BASIC_ILC}
                -DBAS_FILE=${_bas_file}
                "-DEXPECT=${_expect_output}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/../golden/arrays/check_basic_run_output.cmake)
    endif ()
endforeach ()
