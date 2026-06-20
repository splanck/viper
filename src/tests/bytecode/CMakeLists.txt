# SPDX-License-Identifier: GPL-3.0-only
# File: tests/bytecode/CMakeLists.txt
# Purpose: Register bytecode VM parity tests.

viper_add_test(test_bytecode_full_program_parity
        ${CMAKE_CURRENT_LIST_DIR}/test_full_program_parity.cpp
        LIBS viper_bytecode il_vm viper_test_common viper_runtime
        COMPILE_DEFS VIPER_SHARED_IL_CORPUS_DIR="${CMAKE_SOURCE_DIR}/src/tests/shared_corpus/il")
viper_add_test_labels(test_bytecode_full_program_parity vm bytecode conformance)
set_tests_properties(test_bytecode_full_program_parity PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 120)
