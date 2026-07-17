# SPDX-License-Identifier: GPL-3.0-only
# File: tests/conformance/CMakeLists.txt
# Purpose: Register arithmetic conformance test suite.

set(_ZANNA_CONFORMANCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ── Shift conformance (AShr, LShr) ─────────────────────────────────────────
zanna_add_test(test_shift_conformance
        ${_ZANNA_CONFORMANCE_DIR}/ShiftConformanceTests.cpp)
target_link_libraries(test_shift_conformance PRIVATE
        il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
zanna_add_ctest(test_shift_conformance test_shift_conformance)

# ── Sub-width arithmetic (I32/I16 checked ops) ────────────────────────────
zanna_add_test(test_subwidth_arith
        ${_ZANNA_CONFORMANCE_DIR}/SubWidthArithTests.cpp)
target_link_libraries(test_subwidth_arith PRIVATE
        il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
zanna_add_ctest(test_subwidth_arith test_subwidth_arith)

# ── VM/Bytecode shared scalar semantics ──────────────────────────────────
zanna_add_test(test_vm_bytecode_scalar_semantics
        ${_ZANNA_CONFORMANCE_DIR}/VMBytecodeScalarSemanticsTests.cpp)
target_link_libraries(test_vm_bytecode_scalar_semantics PRIVATE
        il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_bytecode zanna_test_common)
zanna_add_ctest(test_vm_bytecode_scalar_semantics test_vm_bytecode_scalar_semantics)

# ── Zia arithmetic conformance (opcode verification) ─────────────────────
zanna_add_test(test_zia_arith_conformance
        ${_ZANNA_CONFORMANCE_DIR}/ZiaArithConformanceTests.cpp)
target_link_libraries(test_zia_arith_conformance PRIVATE ${ZANNA_ZIA_LIBS})
zanna_add_ctest(test_zia_arith_conformance test_zia_arith_conformance)

# ── Cross-layer deterministic equivalence (VM vs ARM64 native) ───────────
if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    _aarch64_add_test_common(test_crosslayer_arith
            ${_ZANNA_CONFORMANCE_DIR}/CrossLayerArithTests.cpp
            "il_codegen_aarch64;zanna_cmd_arm64")
    set_tests_properties(test_crosslayer_arith PROPERTIES
            RUN_SERIAL TRUE TIMEOUT 120 LABELS "slow;conformance")

    # Memory/aggregate + exception-handling cross-layer conformance.
    _aarch64_add_test_common(test_memory_eh_conformance
            ${_ZANNA_CONFORMANCE_DIR}/MemoryEhConformanceTests.cpp
            "il_codegen_aarch64;zanna_cmd_arm64;${ZANNA_IL_CORE_IO_LIBS};il_api")
    set_tests_properties(test_memory_eh_conformance PROPERTIES
            RUN_SERIAL TRUE TIMEOUT 120 LABELS "slow;conformance")
else ()
    # On non-ARM64: build VM-only tests (native tests skip at runtime)
    zanna_add_test(test_crosslayer_arith
            ${_ZANNA_CONFORMANCE_DIR}/CrossLayerArithTests.cpp)
    target_link_libraries(test_crosslayer_arith PRIVATE
            il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_crosslayer_arith test_crosslayer_arith)

    zanna_add_test(test_memory_eh_conformance
            ${_ZANNA_CONFORMANCE_DIR}/MemoryEhConformanceTests.cpp)
    target_link_libraries(test_memory_eh_conformance PRIVATE
            il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common
            ${ZANNA_IL_CORE_IO_LIBS} il_api)
    zanna_add_ctest(test_memory_eh_conformance test_memory_eh_conformance)
endif ()

# ── BASIC arithmetic golden tests ────────────────────────────────────────
set(BASIC_ILC $<TARGET_FILE:zanna>)
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
        zanna_add_ctest(basic_arith_${test_name}
                ${CMAKE_COMMAND}
                -DILC=${BASIC_ILC}
                -DBAS_FILE=${_bas_file}
                "-DEXPECT=${_expect_output}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/../golden/arrays/check_basic_run_output.cmake)
    endif ()
endforeach ()
