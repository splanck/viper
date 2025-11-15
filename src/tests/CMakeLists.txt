# SPDX-License-Identifier: MIT
# File: tests/CMakeLists.txt
# Purpose: Configure test suites.

cmake_minimum_required(VERSION 3.20)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/TestHelpers.cmake)

## Docs lint disabled (no external scripting dependencies)

set(VIPER_TESTS_DIR ${CMAKE_CURRENT_LIST_DIR})

file(GLOB VIPER_TEST_SCRIPT_FILES
        LIST_DIRECTORIES FALSE
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*.sh")
foreach (_viper_test_script IN LISTS VIPER_TEST_SCRIPT_FILES)
    file(
            CHMOD "${_viper_test_script}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
endforeach ()

find_package(Threads REQUIRED)
find_package(GTest QUIET)

set(_viper_testing_gtest_targets)
if (GTest_FOUND)
    list(APPEND _viper_testing_gtest_targets GTest::gtest)
    if (TARGET GTest::gtest_main)
        list(APPEND _viper_testing_gtest_targets GTest::gtest_main)
    endif ()
else ()
    if (TARGET GTest::gtest)
        list(APPEND _viper_testing_gtest_targets GTest::gtest)
    elseif (TARGET gtest)
        list(APPEND _viper_testing_gtest_targets gtest)
    endif ()
    if (TARGET GTest::gtest_main)
        list(APPEND _viper_testing_gtest_targets GTest::gtest_main)
    elseif (TARGET gtest_main)
        list(APPEND _viper_testing_gtest_targets gtest_main)
    endif ()
endif ()

add_library(viper_testing INTERFACE)
target_link_libraries(viper_testing INTERFACE viper_common_opts viper::il_full ${_viper_testing_gtest_targets})

add_library(viper_test_common STATIC
        ${CMAKE_CURRENT_LIST_DIR}/common/CodegenFixture.cpp
        ${CMAKE_CURRENT_LIST_DIR}/common/TestIRBuilder.cpp
        ${CMAKE_CURRENT_LIST_DIR}/common/VmFixture.cpp)
# Limit include paths to avoid clashing with the C++ <version> header on
# case-insensitive filesystems (root contains a VERSION file).
# Expose only tests dir, public headers, generated headers, and src as needed.
target_include_directories(viper_test_common PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(viper_test_common PUBLIC viper_testing il_vm il_build)

## docs_comment_check removed to avoid Python dependency

add_subdirectory(unit)
add_subdirectory(il)
add_subdirectory(vm)
add_subdirectory(perf)
add_subdirectory(tools)
add_subdirectory(basic)
add_subdirectory(golden)
add_subdirectory(e2e)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/codegen/x86_64)

viper_add_ctest(NoAssertFalseGuard
        ${CMAKE_COMMAND}
        -DVIPER_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/src/tests/tools/NoAssertFalseTest.cmake)

add_test(NAME smoke_term_basic
        COMMAND $<TARGET_FILE:ilc> front basic -run ${CMAKE_CURRENT_SOURCE_DIR}/smoke/term_basic.bas)

add_test(NAME basic_sum_no_linenos
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_sum_no_linenos.sh
        $<TARGET_FILE:ilc>)
set_tests_properties(basic_sum_no_linenos
        PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_basic_oop_parsing ${CMAKE_CURRENT_SOURCE_DIR}/unit/BasicOOP_Parsing.cpp)
target_link_libraries(test_basic_oop_parsing fe_basic viper_testing)
add_test(NAME unit_basic_oop_parsing COMMAND test_basic_oop_parsing)

add_executable(test_basic_oop_lowering ${CMAKE_CURRENT_SOURCE_DIR}/unit/BasicOOP_Lowering.cpp)
target_link_libraries(test_basic_oop_lowering fe_basic viper_testing)
add_test(NAME unit_basic_oop_lowering COMMAND test_basic_oop_lowering)

# String array field helpers in OOP
add_executable(test_basic_oop_string_array_field
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_oop_string_array_field.cpp)
target_link_libraries(test_basic_oop_string_array_field PRIVATE fe_basic viper_testing)
viper_add_ctest(test_basic_oop_string_array_field test_basic_oop_string_array_field)

# BUG-040 guard: FUNCTIONS returning class types should return ptr from RETURN var
viper_add_test(
        test_basic_class_return
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_class_return.cpp)
target_link_libraries(test_basic_class_return PRIVATE fe_basic)
viper_add_ctest(test_basic_class_return test_basic_class_return)

viper_add_test(
        test_basic_lower_builtin_dispatch
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_lower_builtin_dispatch.cpp)
target_link_libraries(test_basic_lower_builtin_dispatch PRIVATE fe_basic)
viper_add_ctest(test_basic_lower_builtin_dispatch test_basic_lower_builtin_dispatch)

viper_add_test(
        test_basic_semantic_new_ctor
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_semantic_new_ctor.cpp)
target_link_libraries(test_basic_semantic_new_ctor PRIVATE fe_basic)
viper_add_ctest(test_basic_semantic_new_ctor test_basic_semantic_new_ctor)

viper_add_test(test_run_process_quotes ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_run_process_quotes.cpp)
target_link_libraries(test_run_process_quotes PRIVATE viper_common)
viper_add_ctest(test_run_process_quotes test_run_process_quotes)

add_test(NAME smoke_basic_oop
        COMMAND $<TARGET_FILE:ilc> front basic -run ${CMAKE_CURRENT_SOURCE_DIR}/smoke/basic_oop.bas)

add_test(NAME smoke_basic_point_ctor
        COMMAND $<TARGET_FILE:ilc> front basic -run ${CMAKE_CURRENT_SOURCE_DIR}/smoke/basic_point_ctor.bas)

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
viper_add_basic_namespace_tests()
viper_add_e2e_codegen_tests()
viper_add_golden_suite_tests()

add_test(NAME basic_repros
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_basic_repros.sh
        $<TARGET_FILE:ilc>)
set_tests_properties(basic_repros
        PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
