# SPDX-License-Identifier: MIT
# File: tests/codegen/x86_64/CMakeLists.txt
# Purpose: Build x86-64 backend codegen tests that validate emitted assembly.

if(TARGET test_codegen_x86_64_add_ret)
  return()
endif()

set(_VIPER_CODEGEN_X64_DIR ${CMAKE_CURRENT_LIST_DIR})

viper_add_test(test_codegen_x86_64_add_ret ${_VIPER_CODEGEN_X64_DIR}/test_add_ret.cpp)
target_include_directories(test_codegen_x86_64_add_ret PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_codegen_x86_64_add_ret PRIVATE il_codegen_x86_64)
viper_add_ctest(test_codegen_x86_64_add_ret test_codegen_x86_64_add_ret)
set_tests_properties(test_codegen_x86_64_add_ret PROPERTIES LABELS CodegenX86_64)
