# SPDX-License-Identifier: MIT
# File: tests/codegen/x86_64/CMakeLists.txt
# Purpose: Build smoke tests validating the x86-64 backend text emission.

set(_VIPER_CODEGEN_X86_64_TEST ${VIPER_TESTS_DIR}/codegen/x86_64/test_add_ret.cpp)

viper_add_test(test_codegen_x86_64_add_ret
  ${_VIPER_CODEGEN_X86_64_TEST}
  TEST_NAME codegen_x86_64_add_ret)
target_include_directories(test_codegen_x86_64_add_ret PRIVATE
  ${VIPER_TESTS_DIR}/unit
  ${CMAKE_SOURCE_DIR}/src)
target_sources(test_codegen_x86_64_add_ret PRIVATE
  ${CMAKE_SOURCE_DIR}/src/codegen/x86_64/Backend.cpp
  ${CMAKE_SOURCE_DIR}/src/codegen/x86_64/LowerILToMIR.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SysVTargetShim.cpp)
target_link_libraries(test_codegen_x86_64_add_ret PRIVATE il_codegen_x86_64)

set_tests_properties(codegen_x86_64_add_ret PROPERTIES LABELS CodegenX64)
