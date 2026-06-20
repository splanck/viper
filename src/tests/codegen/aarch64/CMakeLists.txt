# SPDX-License-Identifier: GPL-3.0-only
# File: tests/codegen/aarch64/CMakeLists.txt
# Purpose: Build dedicated AArch64 codegen tests over the shared IL corpus.

viper_add_test(test_codegen_aarch64_shared_corpus
        ${CMAKE_CURRENT_LIST_DIR}/test_shared_il_corpus.cpp
        LIBS viper_cmd_arm64 viper_test_common
        COMPILE_DEFS VIPER_SHARED_IL_CORPUS_DIR="${CMAKE_SOURCE_DIR}/src/tests/shared_corpus/il")
viper_add_test_labels(test_codegen_aarch64_shared_corpus codegen arm64)
set_tests_properties(test_codegen_aarch64_shared_corpus PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 120
        RESOURCE_LOCK aarch64_codegen_artifacts)
