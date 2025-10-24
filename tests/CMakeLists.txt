# SPDX-License-Identifier: MIT
# File: tests/CMakeLists.txt
# Purpose: Configure test suites.

cmake_minimum_required(VERSION 3.20)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/TestHelpers.cmake)

option(IL_ENABLE_DOCS_LINT "Run header/Doxygen docs check (scripts/check_comments.py)" OFF)

set(VIPER_TESTS_DIR ${CMAKE_CURRENT_LIST_DIR})

find_package(Threads REQUIRED)
find_package(GTest QUIET)

set(_viper_testing_gtest_targets)
if(GTest_FOUND)
  list(APPEND _viper_testing_gtest_targets GTest::gtest)
  if(TARGET GTest::gtest_main)
    list(APPEND _viper_testing_gtest_targets GTest::gtest_main)
  endif()
else()
  if(TARGET GTest::gtest)
    list(APPEND _viper_testing_gtest_targets GTest::gtest)
  elseif(TARGET gtest)
    list(APPEND _viper_testing_gtest_targets gtest)
  endif()
  if(TARGET GTest::gtest_main)
    list(APPEND _viper_testing_gtest_targets GTest::gtest_main)
  elseif(TARGET gtest_main)
    list(APPEND _viper_testing_gtest_targets gtest_main)
  endif()
endif()

add_library(viper_testing INTERFACE)
target_link_libraries(viper_testing INTERFACE viper_common_opts viper::il_full ${_viper_testing_gtest_targets})

if(IL_ENABLE_DOCS_LINT)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  execute_process(
    COMMAND git rev-parse --is-inside-work-tree
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE GIT_OK
    OUTPUT_QUIET ERROR_QUIET)
  if(GIT_OK EQUAL 0)
    viper_add_ctest(docs_comment_check
      ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/scripts/check_comments.py)
    set_tests_properties(docs_comment_check PROPERTIES LABELS Docs)
  else()
    message(STATUS "Skipping docs_comment_check (not a Git working tree)")
  endif()
endif()

add_subdirectory(unit)
add_subdirectory(il)
add_subdirectory(vm)
add_subdirectory(perf)
add_subdirectory(tools)
add_subdirectory(basic)
add_subdirectory(golden)
add_subdirectory(codegen/x86_64)
add_subdirectory(e2e)

viper_add_ctest(NoAssertFalseGuard
  ${CMAKE_COMMAND}
  -DVIPER_SOURCE_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_SOURCE_DIR}/tests/tools/NoAssertFalseTest.cmake)

add_test(NAME smoke_term_basic
  COMMAND $<TARGET_FILE:ilc> front basic -run ${CMAKE_CURRENT_SOURCE_DIR}/smoke/term_basic.bas)

add_executable(test_basic_oop_parsing ${CMAKE_CURRENT_SOURCE_DIR}/unit/BasicOOP_Parsing.cpp)
target_link_libraries(test_basic_oop_parsing fe_basic viper_testing)
add_test(NAME unit_basic_oop_parsing COMMAND test_basic_oop_parsing)

add_executable(test_basic_oop_lowering ${CMAKE_CURRENT_SOURCE_DIR}/unit/BasicOOP_Lowering.cpp)
target_link_libraries(test_basic_oop_lowering fe_basic viper_testing)
add_test(NAME unit_basic_oop_lowering COMMAND test_basic_oop_lowering)

add_test(NAME smoke_basic_oop
         COMMAND $<TARGET_FILE:ilc> front basic -run ${CMAKE_CURRENT_SOURCE_DIR}/smoke/basic_oop.bas)

viper_add_unit_support_tests()
viper_add_il_core_tests()
viper_add_basic_early_tests()
viper_add_il_utils_test()
viper_add_tools_tests()
viper_add_vm_debug_tests()
viper_add_il_analysis_tests()
viper_add_il_verify_examples()
viper_add_e2e_vm_examples_smoke()
viper_add_runtime_tests()
viper_add_e2e_vm_suite()
viper_add_il_invalid_tests()
viper_add_basic_main_tests()
viper_add_vm_unit_tests()
viper_add_il_liveness_tests()
viper_add_vm_path_tests()
viper_add_basic_ast_golden_tests()
viper_add_basic_lex_golden_tests()
viper_add_basic_to_il_golden_tests()
viper_add_basic_strings_test()
viper_add_basic_semantics_golden_tests()
viper_add_basic_proc_tests()
viper_add_basic_call_tests()
viper_add_basic_integration_tests()
viper_add_e2e_codegen_tests()
viper_add_golden_suite_tests()
