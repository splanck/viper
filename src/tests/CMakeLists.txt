# SPDX-License-Identifier: GPL-3.0-only
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
add_executable(test_target_aarch64 ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_target_aarch64.cpp)
target_link_libraries(test_target_aarch64 PRIVATE viper_testing il_codegen_aarch64)
viper_add_ctest(test_target_aarch64 $<TARGET_FILE:test_target_aarch64>)

# Minimal AArch64 emission smoke test
add_executable(test_emit_aarch64_minimal ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_emit_aarch64_minimal.cpp)
target_link_libraries(test_emit_aarch64_minimal PRIVATE viper_testing il_codegen_aarch64)
viper_add_ctest(test_emit_aarch64_minimal $<TARGET_FILE:test_emit_aarch64_minimal>)

# Minimal AArch64 MIR emission smoke test
add_executable(test_emit_aarch64_mir_minimal ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_emit_aarch64_mir_minimal.cpp)
target_link_libraries(test_emit_aarch64_mir_minimal PRIVATE viper_testing il_codegen_aarch64)
viper_add_ctest(test_emit_aarch64_mir_minimal $<TARGET_FILE:test_emit_aarch64_mir_minimal>)

# FramePlan emission test
add_executable(test_emit_aarch64_frameplan ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_emit_aarch64_frameplan.cpp)
target_link_libraries(test_emit_aarch64_frameplan PRIVATE viper_testing il_codegen_aarch64)
viper_add_ctest(test_emit_aarch64_frameplan $<TARGET_FILE:test_emit_aarch64_frameplan>)

# MIR bitwise rr emission test
add_executable(test_emit_aarch64_mir_bitwise ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_emit_aarch64_mir_bitwise.cpp)
target_link_libraries(test_emit_aarch64_mir_bitwise PRIVATE viper_testing il_codegen_aarch64)
viper_add_ctest(test_emit_aarch64_mir_bitwise $<TARGET_FILE:test_emit_aarch64_mir_bitwise>)

# MIR branches emission test
add_executable(test_emit_aarch64_mir_branches ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_emit_aarch64_mir_branches.cpp)
target_link_libraries(test_emit_aarch64_mir_branches PRIVATE viper_testing il_codegen_aarch64)
viper_add_ctest(test_emit_aarch64_mir_branches $<TARGET_FILE:test_emit_aarch64_mir_branches>)

# MIR frame plan emission through emitFunction
add_executable(test_emit_aarch64_mir_frameplan ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_emit_aarch64_mir_frameplan.cpp)
target_link_libraries(test_emit_aarch64_mir_frameplan PRIVATE viper_testing il_codegen_aarch64)
viper_add_ctest(test_emit_aarch64_mir_frameplan $<TARGET_FILE:test_emit_aarch64_mir_frameplan>)

# CLI smoke for arm64 -S (ret 0)
add_executable(test_codegen_arm64_cli ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_cli.cpp)
target_link_libraries(test_codegen_arm64_cli PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_cli $<TARGET_FILE:test_codegen_arm64_cli>)

add_executable(test_codegen_arm64_add_params ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_add_params.cpp)
target_link_libraries(test_codegen_arm64_add_params PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_add_params $<TARGET_FILE:test_codegen_arm64_add_params>)

add_executable(test_codegen_arm64_ret_param ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_ret_param.cpp)
target_link_libraries(test_codegen_arm64_ret_param PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_ret_param $<TARGET_FILE:test_codegen_arm64_ret_param>)

add_executable(test_codegen_arm64_add_imm ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_add_imm.cpp)
target_link_libraries(test_codegen_arm64_add_imm PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_add_imm $<TARGET_FILE:test_codegen_arm64_add_imm>)

add_executable(test_codegen_arm64_bitwise ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_bitwise.cpp)
target_link_libraries(test_codegen_arm64_bitwise PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_bitwise $<TARGET_FILE:test_codegen_arm64_bitwise>)

add_executable(test_codegen_arm64_params_wide ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_params_wide.cpp)
target_link_libraries(test_codegen_arm64_params_wide PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_params_wide $<TARGET_FILE:test_codegen_arm64_params_wide>)

add_executable(test_codegen_arm64_ovf ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_ovf.cpp)
target_link_libraries(test_codegen_arm64_ovf PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_ovf $<TARGET_FILE:test_codegen_arm64_ovf>)

add_executable(test_codegen_arm64_shift_imm ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_shift_imm.cpp)
target_link_libraries(test_codegen_arm64_shift_imm PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_shift_imm $<TARGET_FILE:test_codegen_arm64_shift_imm>)

add_executable(test_codegen_arm64_stack_locals ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_stack_locals.cpp)
target_link_libraries(test_codegen_arm64_stack_locals PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_stack_locals $<TARGET_FILE:test_codegen_arm64_stack_locals>)

add_executable(test_codegen_arm64_icmp ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_icmp.cpp)
target_link_libraries(test_codegen_arm64_icmp PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_icmp $<TARGET_FILE:test_codegen_arm64_icmp>)

add_executable(test_codegen_arm64_large_imm ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_large_imm.cpp)
target_link_libraries(test_codegen_arm64_large_imm PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_large_imm $<TARGET_FILE:test_codegen_arm64_large_imm>)

add_executable(test_codegen_arm64_icmp_imm ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_icmp_imm.cpp)
target_link_libraries(test_codegen_arm64_icmp_imm PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_icmp_imm $<TARGET_FILE:test_codegen_arm64_icmp_imm>)

# cbr lowering tests for arm64 CLI
add_executable(test_codegen_arm64_cbr ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_cbr.cpp)
target_link_libraries(test_codegen_arm64_cbr PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_cbr $<TARGET_FILE:test_codegen_arm64_cbr>)

# call lowering smoke test for arm64 CLI (bl callee)
add_executable(test_codegen_arm64_call ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_call.cpp)
target_link_libraries(test_codegen_arm64_call PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_call $<TARGET_FILE:test_codegen_arm64_call>)

# call arg marshalling tests
add_executable(test_codegen_arm64_call_args ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_call_args.cpp)
target_link_libraries(test_codegen_arm64_call_args PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_call_args $<TARGET_FILE:test_codegen_arm64_call_args>)

# call args with a single non-entry temp
add_executable(test_codegen_arm64_call_temp_args ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_call_temp_args.cpp)
target_link_libraries(test_codegen_arm64_call_temp_args PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
viper_add_ctest(test_codegen_arm64_call_temp_args $<TARGET_FILE:test_codegen_arm64_call_temp_args>)

## call with >8 integer args uses stack area (pending enable after backend support hardens)
# add_executable(test_codegen_arm64_call_stack_args ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_call_stack_args.cpp)
# target_link_libraries(test_codegen_arm64_call_stack_args PRIVATE viper_testing il_codegen_aarch64 ilc_cmd_arm64)
# viper_add_ctest(test_codegen_arm64_call_stack_args $<TARGET_FILE:test_codegen_arm64_call_stack_args>)

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

# Object array field helpers in OOP
add_executable(test_basic_oop_object_array_field
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_oop_object_array_field.cpp)
target_link_libraries(test_basic_oop_object_array_field PRIVATE fe_basic viper_testing)
viper_add_ctest(test_basic_oop_object_array_field test_basic_oop_object_array_field)

# Global string array assignment from SUB uses string helper
add_executable(test_basic_global_string_array_sub_store
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_global_string_array_sub_store.cpp)
target_link_libraries(test_basic_global_string_array_sub_store PRIVATE fe_basic viper_testing)
viper_add_ctest(test_basic_global_string_array_sub_store test_basic_global_string_array_sub_store)

# SELECT CASE object assignment retains object
add_executable(test_basic_select_case_object_assign
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_select_case_object_assign.cpp)
target_link_libraries(test_basic_select_case_object_assign PRIVATE fe_basic viper_testing)
viper_add_ctest(test_basic_select_case_object_assign test_basic_select_case_object_assign)

# INPUT typing for SINGLE conversion
add_executable(test_basic_input_typing
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_input_typing.cpp)
target_link_libraries(test_basic_input_typing PRIVATE fe_basic viper_testing)
viper_add_ctest(test_basic_input_typing test_basic_input_typing)

# Nested member method call resolution
add_executable(test_basic_nested_member_method_call
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_nested_member_method_call.cpp)
target_link_libraries(test_basic_nested_member_method_call PRIVATE fe_basic viper_testing)
viper_add_ctest(test_basic_nested_member_method_call test_basic_nested_member_method_call)

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

# Runtime class catalog and binding tests
add_executable(test_runtime_class_catalog ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime_classes/TestRuntimeClassCatalog.cpp)
target_link_libraries(test_runtime_class_catalog PRIVATE viper_testing fe_basic)
viper_add_ctest(test_runtime_class_catalog test_runtime_class_catalog)

add_executable(test_runtime_property_binding ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime_classes/TestPropertyBinding.cpp)
target_link_libraries(test_runtime_property_binding PRIVATE viper_testing fe_basic)
viper_add_ctest(test_runtime_property_binding test_runtime_property_binding)

add_executable(test_runtime_method_binding ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime_classes/TestMethodBinding.cpp)
target_link_libraries(test_runtime_method_binding PRIVATE viper_testing fe_basic)
viper_add_ctest(test_runtime_method_binding test_runtime_method_binding)

add_executable(test_runtime_catalog_targets_resolve ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime_classes/TestCatalogTargetsResolve.cpp)
target_link_libraries(test_runtime_catalog_targets_resolve PRIVATE viper_testing fe_basic)
viper_add_ctest(test_runtime_catalog_targets_resolve test_runtime_catalog_targets_resolve)

# Runtime property diagnostics (read-only setter)
add_executable(test_frontends_basic_runtime_property_diag ${CMAKE_CURRENT_SOURCE_DIR}/frontends/basic/RuntimePropertyDiagTests.cpp)
target_link_libraries(test_frontends_basic_runtime_property_diag PRIVATE viper_testing fe_basic)
viper_add_ctest(test_frontends_basic_runtime_property_diag test_frontends_basic_runtime_property_diag)

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

# Milestone D unit tests (OOP)
add_executable(test_oop_static_members ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestStaticMembers.cpp)
target_link_libraries(test_oop_static_members PRIVATE fe_basic viper_testing)
viper_add_ctest(test_oop_static_members test_oop_static_members)

add_executable(test_oop_properties ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestProperties.cpp)
target_link_libraries(test_oop_properties PRIVATE fe_basic viper_testing)
viper_add_ctest(test_oop_properties test_oop_properties)

add_executable(test_oop_overloads ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestOverloads.cpp)
target_link_libraries(test_oop_overloads PRIVATE fe_basic viper_testing)
viper_add_ctest(test_oop_overloads test_oop_overloads)

# Destructor + dispose tests
add_executable(test_oop_destructor_dispose ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestDestructorDispose.cpp)
target_link_libraries(test_oop_destructor_dispose PRIVATE fe_basic viper_testing)
viper_add_ctest(test_oop_destructor_dispose test_oop_destructor_dispose)

add_executable(test_oop_destructor_chain ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestDestructorChain.cpp)
target_link_libraries(test_oop_destructor_chain PRIVATE fe_basic viper_testing viper_test_common)
viper_add_ctest(test_oop_destructor_chain test_oop_destructor_chain)

add_executable(test_oop_dispose ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestDispose.cpp)
target_link_libraries(test_oop_dispose PRIVATE fe_basic viper_testing viper_test_common)
viper_add_ctest(test_oop_dispose test_oop_dispose)

add_test(NAME basic_repros
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_basic_repros.sh
        $<TARGET_FILE:ilc>)
set_tests_properties(basic_repros
        PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
