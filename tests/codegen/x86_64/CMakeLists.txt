# SPDX-License-Identifier: MIT
# File: tests/codegen/x86_64/CMakeLists.txt
# Purpose: Build the x86-64 codegen smoke tests that exercise the adapter
#          ILModule facade and verify minimal assembly output.

viper_add_test(
  test_codegen_x86_64_add_ret
  ${VIPER_TESTS_DIR}/codegen/x86_64/test_add_ret.cpp
  TEST_NAME codegen_x86_64_add_ret)

target_link_libraries(test_codegen_x86_64_add_ret PRIVATE il_codegen_x86_64)

viper_add_test(
  test_codegen_x86_64_select_cmov
  ${VIPER_TESTS_DIR}/codegen/x86_64/test_select_cmov.cpp
  TEST_NAME codegen_x86_64_select_cmov)

target_link_libraries(test_codegen_x86_64_select_cmov PRIVATE il_codegen_x86_64)

viper_add_test(
  test_codegen_x86_64_string_literal
  ${VIPER_TESTS_DIR}/codegen/x86_64/test_string_literal.cpp
  TEST_NAME codegen_x86_64_string_literal)

target_link_libraries(test_codegen_x86_64_string_literal PRIVATE il_codegen_x86_64)
