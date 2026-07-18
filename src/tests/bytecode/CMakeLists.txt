# SPDX-License-Identifier: GPL-3.0-only
# File: tests/bytecode/CMakeLists.txt
# Purpose: Register bytecode VM parity tests.

zanna_add_test(test_bytecode_full_program_parity
        ${CMAKE_CURRENT_LIST_DIR}/test_full_program_parity.cpp
        LIBS zanna_bytecode il_vm zanna_test_common zanna_runtime
        COMPILE_DEFS ZANNA_SHARED_IL_CORPUS_DIR="${CMAKE_SOURCE_DIR}/src/tests/shared_corpus/il")
zanna_add_test_labels(test_bytecode_full_program_parity vm bytecode conformance)
set_tests_properties(test_bytecode_full_program_parity PROPERTIES
        RUN_SERIAL TRUE)
if (IL_SANITIZE_ADDRESS OR IL_SANITIZE_UNDEFINED OR IL_SANITIZE_THREAD)
    set_property(TEST test_bytecode_full_program_parity PROPERTY TIMEOUT
            ${ZANNA_SANITIZER_TEST_TIMEOUT})
else ()
    set_property(TEST test_bytecode_full_program_parity PROPERTY TIMEOUT 120)
endif ()
