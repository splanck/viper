# SPDX-License-Identifier: GPL-3.0-only
# File: tests/unit/CMakeLists.txt
# Purpose: Build standalone unit test executables.

set(VIPER_UNIT_SUPPORT_LIBS viper_support)
set(VIPER_UNIT_SUPPORT_LIBS ${VIPER_UNIT_SUPPORT_LIBS} PARENT_SCOPE)
set(VIPER_RUNTIME_TEST_LIBS viper_runtime)
set(VIPER_RUNTIME_TEST_LIBS ${VIPER_RUNTIME_TEST_LIBS} PARENT_SCOPE)

function(viper_add_unit_support_tests)
    if (TARGET test_support)
        return()
    endif ()

    viper_add_test(test_support ${VIPER_TESTS_DIR}/unit/test_support.cpp)
    target_link_libraries(test_support PRIVATE ${VIPER_UNIT_SUPPORT_LIBS})
    viper_add_ctest(test_support test_support)
endfunction()

if (NOT TARGET test_basic_terminal_scan)
    viper_add_test(test_basic_terminal_scan ${VIPER_TESTS_DIR}/unit/BasicTerminalScanTest.cpp)
    target_link_libraries(test_basic_terminal_scan PRIVATE fe_basic)
    viper_add_ctest(test_basic_terminal_scan test_basic_terminal_scan)
endif ()

# NOTE: Tests for verifier relaxations (partial CBr branch-args, str-to-ptr) removed
# because the verifier does not currently support these relaxations.

# Builtin extern resolution tests (resolver + diagnostics)
if (NOT TARGET test_basic_builtin_extern_resolution)
    viper_add_test(test_basic_builtin_extern_resolution ${VIPER_TESTS_DIR}/unit/test_basic_builtin_extern_resolution.cpp)
    target_link_libraries(test_basic_builtin_extern_resolution PRIVATE fe_basic)
    viper_add_ctest(test_basic_builtin_extern_resolution test_basic_builtin_extern_resolution)
endif ()

# Parser single-index array test (guards HIGH-2 fix)
if (NOT TARGET test_basic_parse_array_single_index)
    viper_add_test(test_basic_parse_array_single_index ${VIPER_TESTS_DIR}/unit/test_basic_parse_array_single_index.cpp)
    target_link_libraries(test_basic_parse_array_single_index PRIVATE fe_basic)
    viper_add_ctest(test_basic_parse_array_single_index test_basic_parse_array_single_index)
endif ()

# Builtins ARGC/ARG$/COMMAND$ lowering test
if (NOT TARGET test_basic_builtins_args_cmd)
    viper_add_test(test_basic_builtins_args_cmd ${VIPER_TESTS_DIR}/unit/test_basic_builtins_args_cmd.cpp)
    target_link_libraries(test_basic_builtins_args_cmd PRIVATE fe_basic)
    viper_add_ctest(test_basic_builtins_args_cmd test_basic_builtins_args_cmd)
endif ()

if (NOT TARGET test_basic_builtins_args_cmd_semantics)
    viper_add_test(test_basic_builtins_args_cmd_semantics ${VIPER_TESTS_DIR}/unit/test_basic_builtins_args_cmd_semantics.cpp)
    target_link_libraries(test_basic_builtins_args_cmd_semantics PRIVATE fe_basic)
    viper_add_ctest(test_basic_builtins_args_cmd_semantics test_basic_builtins_args_cmd_semantics)
endif ()

# Runtime StringBuilder bridge tests
if (NOT TARGET test_stringbuilder_bridge)
    viper_add_test(test_stringbuilder_bridge ${VIPER_TESTS_DIR}/unit/test_stringbuilder_bridge.c)
    target_link_libraries(test_stringbuilder_bridge PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_stringbuilder_bridge test_stringbuilder_bridge)
endif ()

if (NOT TARGET test_basic_builtins_arg_get_typing)
    viper_add_test(test_basic_builtins_arg_get_typing ${VIPER_TESTS_DIR}/unit/test_basic_builtins_arg_get_typing.cpp)
    target_link_libraries(test_basic_builtins_arg_get_typing PRIVATE fe_basic)
    viper_add_ctest(test_basic_builtins_arg_get_typing test_basic_builtins_arg_get_typing)
endif ()

if (NOT TARGET test_basic_parse_namespace)
    viper_add_test(test_basic_parse_namespace ${VIPER_TESTS_DIR}/unit/test_basic_parse_namespace.cpp)
    target_link_libraries(test_basic_parse_namespace PRIVATE fe_basic)
    viper_add_ctest(test_basic_parse_namespace test_basic_parse_namespace)
endif ()

if (NOT TARGET test_basic_parse_using)
    viper_add_test(test_basic_parse_using ${VIPER_TESTS_DIR}/unit/test_basic_parse_using.cpp)
    target_link_libraries(test_basic_parse_using PRIVATE fe_basic)
    viper_add_ctest(test_basic_parse_using test_basic_parse_using)
endif ()

if (NOT TARGET test_namespace_registry)
    viper_add_test(test_namespace_registry ${VIPER_TESTS_DIR}/unit/test_namespace_registry.cpp)
    target_link_libraries(test_namespace_registry PRIVATE fe_basic)
    viper_add_ctest(test_namespace_registry test_namespace_registry)
endif ()

if (NOT TARGET test_using_context)
    viper_add_test(test_using_context ${VIPER_TESTS_DIR}/unit/test_using_context.cpp)
    target_link_libraries(test_using_context PRIVATE fe_basic)
    viper_add_ctest(test_using_context test_using_context)
endif ()

if (NOT TARGET test_type_resolver)
    viper_add_test(test_type_resolver ${VIPER_TESTS_DIR}/unit/test_type_resolver.cpp)
    target_link_libraries(test_type_resolver PRIVATE fe_basic)
    viper_add_ctest(test_type_resolver test_type_resolver)
endif ()

if (NOT TARGET test_ns_registry_builder)
    viper_add_test(test_ns_registry_builder ${VIPER_TESTS_DIR}/unit/test_ns_registry_builder.cpp)
    target_link_libraries(test_ns_registry_builder PRIVATE fe_basic)
    viper_add_ctest(test_ns_registry_builder test_ns_registry_builder)
endif ()

if (NOT TARGET test_using_semantics)
    viper_add_test(test_using_semantics ${VIPER_TESTS_DIR}/unit/test_using_semantics.cpp)
    target_link_libraries(test_using_semantics PRIVATE fe_basic)
    viper_add_ctest(test_using_semantics test_using_semantics)
endif ()

if (NOT TARGET test_ns_resolve_pass)
    viper_add_test(test_ns_resolve_pass ${VIPER_TESTS_DIR}/unit/test_ns_resolve_pass.cpp)
    target_link_libraries(test_ns_resolve_pass PRIVATE fe_basic)
    viper_add_ctest(test_ns_resolve_pass test_ns_resolve_pass)
endif ()

if (NOT TARGET test_lowerer_namespace)
    viper_add_test(test_lowerer_namespace ${VIPER_TESTS_DIR}/unit/test_lowerer_namespace.cpp)
    target_link_libraries(test_lowerer_namespace PRIVATE fe_basic)
    viper_add_ctest(test_lowerer_namespace test_lowerer_namespace)
endif ()

if (NOT TARGET test_namespace_diagnostics)
    viper_add_test(test_namespace_diagnostics ${VIPER_TESTS_DIR}/unit/test_namespace_diagnostics.cpp)
    target_link_libraries(test_namespace_diagnostics PRIVATE fe_basic)
    viper_add_ctest(test_namespace_diagnostics test_namespace_diagnostics)
endif ()

if (NOT TARGET test_namespace_integration)
    viper_add_test(test_namespace_integration ${VIPER_TESTS_DIR}/unit/test_namespace_integration.cpp)
    target_link_libraries(test_namespace_integration PRIVATE fe_basic)
    viper_add_ctest(test_namespace_integration test_namespace_integration)
endif ()

# Built-in external types catalog recognition
if (NOT TARGET test_builtin_external_types)
    viper_add_test(test_builtin_external_types ${VIPER_TESTS_DIR}/unit/types/TestBuiltinExternalTypes.cpp)
    target_link_libraries(test_builtin_external_types PRIVATE fe_basic)
    viper_add_ctest(test_builtin_external_types test_builtin_external_types)
endif ()

if (NOT TARGET test_using_compiletime_only)
    viper_add_test(test_using_compiletime_only ${VIPER_TESTS_DIR}/unit/test_using_compiletime_only.cpp)
    target_link_libraries(test_using_compiletime_only PRIVATE fe_basic)
    viper_add_ctest(test_using_compiletime_only test_using_compiletime_only)
endif ()

if (NOT TARGET test_basic_proc_diagnostics)
    viper_add_test(test_basic_proc_diagnostics ${VIPER_TESTS_DIR}/unit/test_basic_proc_diagnostics.cpp)
    target_link_libraries(test_basic_proc_diagnostics PRIVATE fe_basic)
    viper_add_ctest(test_basic_proc_diagnostics test_basic_proc_diagnostics)
endif ()

if (NOT TARGET test_basic_ns_case_insensitive)
    viper_add_test(test_basic_ns_case_insensitive ${VIPER_TESTS_DIR}/unit/test_basic_ns_case_insensitive.cpp)
    target_link_libraries(test_basic_ns_case_insensitive PRIVATE fe_basic)
    viper_add_ctest(test_basic_ns_case_insensitive test_basic_ns_case_insensitive)
endif ()

if (NOT TARGET test_basic_il_qualified_names)
    viper_add_test(test_basic_il_qualified_names ${VIPER_TESTS_DIR}/unit/test_basic_il_qualified_names.cpp)
    target_link_libraries(test_basic_il_qualified_names PRIVATE fe_basic)
    viper_add_ctest(test_basic_il_qualified_names test_basic_il_qualified_names)
endif ()

if (NOT TARGET test_ident_util)
    viper_add_test(test_ident_util ${VIPER_TESTS_DIR}/unit/test_ident_util.cpp)
    target_link_libraries(test_ident_util PRIVATE fe_basic)
    viper_add_ctest(test_ident_util test_ident_util)
endif ()

# Bug #021 fix: Method call arguments evaluated only once
if (NOT TARGET test_basic_method_call_arg_single_eval)
    viper_add_test(test_basic_method_call_arg_single_eval ${VIPER_TESTS_DIR}/unit/test_basic_method_call_arg_single_eval.cpp)
    target_link_libraries(test_basic_method_call_arg_single_eval PRIVATE fe_basic)
    viper_add_ctest(test_basic_method_call_arg_single_eval test_basic_method_call_arg_single_eval)
endif ()

# Tail-call tests (enable TCO for these targets)
if (NOT TARGET test_vm_tailcall)
    viper_add_test(test_vm_tailcall ${VIPER_TESTS_DIR}/unit/VM_TailCallTests.cpp)
    target_link_libraries(test_vm_tailcall PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB} viper_test_common)
    target_compile_definitions(test_vm_tailcall PRIVATE VIPER_VM_TAILCALL=1)
    viper_add_ctest(test_vm_tailcall test_vm_tailcall)
endif ()

# Runner stepping API tests
if (NOT TARGET test_vm_step_api)
    viper_add_test(test_vm_step_api ${VIPER_TESTS_DIR}/unit/VM_StepApiTests.cpp)
    # Link directly to il_vm to avoid relying on VM vars ordering
    target_link_libraries(test_vm_step_api PRIVATE il_build il_vm viper_support)
    viper_add_ctest(test_vm_step_api test_vm_step_api)
endif ()

if (NOT TARGET test_vm_step_tco)
    viper_add_test(test_vm_step_tco ${VIPER_TESTS_DIR}/unit/VM_StepTcoTests.cpp)
    target_link_libraries(test_vm_step_tco PRIVATE il_build il_vm viper_support)
    target_compile_definitions(test_vm_step_tco PRIVATE VIPER_VM_TAILCALL=1)
    viper_add_ctest(test_vm_step_tco test_vm_step_tco)
endif ()

if (NOT TARGET test_vm_tailcall_eh)
    viper_add_test(test_vm_tailcall_eh ${VIPER_TESTS_DIR}/unit/VM_TailCallEhTests.cpp)
    target_link_libraries(test_vm_tailcall_eh PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB} viper_test_common)
    target_compile_definitions(test_vm_tailcall_eh PRIVATE VIPER_VM_TAILCALL=1)
    viper_add_ctest(test_vm_tailcall_eh test_vm_tailcall_eh)
endif ()

# Opcode counts tests
if (NOT TARGET test_vm_opcode_counts)
    viper_add_test(test_vm_opcode_counts ${VIPER_TESTS_DIR}/unit/VM_OpcodeCountsTests.cpp)
    target_link_libraries(test_vm_opcode_counts PRIVATE il_build il_vm viper_support)
    viper_add_ctest(test_vm_opcode_counts test_vm_opcode_counts)
endif ()

# Opcode profiling test
if (NOT TARGET test_vm_opcode_profiling)
    viper_add_test(test_vm_opcode_profiling ${VIPER_TESTS_DIR}/unit/test_vm_opcode_profiling.cpp)
    target_link_libraries(test_vm_opcode_profiling PRIVATE viper_il_full il_vm il_api)
    viper_add_ctest(test_vm_opcode_profiling test_vm_opcode_profiling)
endif ()

# Runtime class catalog seeding test
if (NOT TARGET test_runtime_class_seeding)
    viper_add_test(test_runtime_class_seeding ${VIPER_TESTS_DIR}/unit/test_runtime_class_seeding.cpp)
    target_link_libraries(test_runtime_class_seeding PRIVATE fe_basic)
    viper_add_ctest(test_runtime_class_seeding test_runtime_class_seeding)
endif ()

# Runtime class catalog smoke test
if (NOT TARGET test_runtime_classes_catalog)
    viper_add_test(test_runtime_classes_catalog ${VIPER_TESTS_DIR}/unit/test_runtime_classes_catalog.cpp)
    # Link with frontend to ensure catalog is reachable in front-end contexts
    target_link_libraries(test_runtime_classes_catalog PRIVATE fe_basic)
    viper_add_ctest(test_runtime_classes_catalog test_runtime_classes_catalog)
endif ()

# New unit tests for RC-B7 runtime-class pipeline
if (NOT TARGET test_catalog_basic)
    viper_add_test(test_catalog_basic ${VIPER_TESTS_DIR}/unit/runtime_classes/TestCatalog.cpp)
    target_link_libraries(test_catalog_basic PRIVATE fe_basic)
    viper_add_ctest(test_catalog_basic test_catalog_basic)
endif ()

if (NOT TARGET test_property_index_basic)
    viper_add_test(test_property_index_basic ${VIPER_TESTS_DIR}/unit/runtime_classes/TestPropertyIndex.cpp)
    target_link_libraries(test_property_index_basic PRIVATE fe_basic)
    viper_add_ctest(test_property_index_basic test_property_index_basic)
endif ()

if (NOT TARGET test_method_index_basic)
    viper_add_test(test_method_index_basic ${VIPER_TESTS_DIR}/unit/runtime_classes/TestMethodIndex.cpp)
    target_link_libraries(test_method_index_basic PRIVATE fe_basic)
    viper_add_ctest(test_method_index_basic test_method_index_basic)
endif ()

if (NOT TARGET test_lowering_runtime_class_len)
    viper_add_test(test_lowering_runtime_class_len ${VIPER_TESTS_DIR}/unit/runtime_classes/TestLowering.cpp)
    target_link_libraries(test_lowering_runtime_class_len PRIVATE fe_basic)
    viper_add_ctest(test_lowering_runtime_class_len test_lowering_runtime_class_len)
endif ()

# System.Text.StringBuilder binding tests (ctor and Capacity getter)
if (NOT TARGET test_stringbuilder_binding)
    viper_add_test(test_stringbuilder_binding ${VIPER_TESTS_DIR}/unit/runtime_classes/TestStringBuilderBinding.cpp)
    target_link_libraries(test_stringbuilder_binding PRIVATE fe_basic)
    viper_add_ctest(test_stringbuilder_binding test_stringbuilder_binding)
endif ()

# System.IO.File binding tests
if (NOT TARGET test_runtime_class_file_binding)
    viper_add_test(test_runtime_class_file_binding ${VIPER_TESTS_DIR}/unit/runtime_classes/TestRuntimeClassFileBinding.cpp)
    target_link_libraries(test_runtime_class_file_binding PRIVATE fe_basic)
    viper_add_ctest(test_runtime_class_file_binding test_runtime_class_file_binding)
endif ()

# System.Collections.List binding tests
if (NOT TARGET test_runtime_class_list_binding)
    viper_add_test(test_runtime_class_list_binding ${VIPER_TESTS_DIR}/unit/runtime_classes/TestRuntimeClassListBinding.cpp)
    target_link_libraries(test_runtime_class_list_binding PRIVATE fe_basic)
    viper_add_ctest(test_runtime_class_list_binding test_runtime_class_list_binding)
endif ()

# Viper.Console binding tests
if (NOT TARGET test_console_binding)
    viper_add_test(test_console_binding ${VIPER_TESTS_DIR}/unit/runtime_classes/TestConsoleBinding.cpp)
    target_link_libraries(test_console_binding PRIVATE fe_basic)
    viper_add_ctest(test_console_binding test_console_binding)
endif ()

# Viper.Convert binding tests
if (NOT TARGET test_convert_binding)
    viper_add_test(test_convert_binding ${VIPER_TESTS_DIR}/unit/runtime_classes/TestConvertBinding.cpp)
    target_link_libraries(test_convert_binding PRIVATE fe_basic)
    viper_add_ctest(test_convert_binding test_convert_binding)
endif ()

if (NOT TARGET test_runtime_class_tostring_user)
    viper_add_test(test_runtime_class_tostring_user ${VIPER_TESTS_DIR}/unit/runtime_classes/TestRuntimeClassToString.cpp)
    target_link_libraries(test_runtime_class_tostring_user PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_runtime_class_tostring_user test_runtime_class_tostring_user)
endif ()


# Poll callback tests
if (NOT TARGET test_vm_poll_callback)
    viper_add_test(test_vm_poll_callback ${VIPER_TESTS_DIR}/unit/VM_PollCallbackTests.cpp)
    target_link_libraries(test_vm_poll_callback PRIVATE il_build il_vm viper_support)
    viper_add_ctest(test_vm_poll_callback test_vm_poll_callback)
endif ()

# Dispatch strategy equivalence test
if (NOT TARGET test_vm_dispatch_equivalence)
    viper_add_test(test_vm_dispatch_equivalence ${VIPER_TESTS_DIR}/unit/test_vm_dispatch_equivalence.cpp)
    target_link_libraries(test_vm_dispatch_equivalence PRIVATE il_build il_vm viper_support)
    viper_add_ctest(test_vm_dispatch_equivalence test_vm_dispatch_equivalence)
endif ()

# Extern registry tests
if (NOT TARGET test_vm_extern_registry)
    viper_add_test(test_vm_extern_registry ${VIPER_TESTS_DIR}/unit/VM_ExternRegistryTests.cpp)
    target_link_libraries(test_vm_extern_registry PRIVATE il_build il_vm viper_support)
    viper_add_ctest(test_vm_extern_registry test_vm_extern_registry)
endif ()

# Memory watch tests
if (NOT TARGET test_vm_mem_watch)
    viper_add_test(test_vm_mem_watch ${VIPER_TESTS_DIR}/unit/VM_MemWatchTests.cpp)
    target_link_libraries(test_vm_mem_watch PRIVATE il_build il_vm viper_support)
    viper_add_ctest(test_vm_mem_watch test_vm_mem_watch)
endif ()

if (NOT TARGET test_integer_helpers)
    viper_add_test(test_integer_helpers ${VIPER_TESTS_DIR}/unit/test_integer_helpers.cpp)
    target_link_libraries(test_integer_helpers PRIVATE ${VIPER_UNIT_SUPPORT_LIBS})
    viper_add_ctest(test_integer_helpers test_integer_helpers)
endif ()

if (NOT TARGET test_codegen_x86_64_passes)
    viper_add_test(
            test_codegen_x86_64_passes
            ${VIPER_TESTS_DIR}/unit/test_codegen_x86_64_passes.cpp)
    target_link_libraries(test_codegen_x86_64_passes PRIVATE viper_codegen_x86_64)
    viper_add_ctest(test_codegen_x86_64_passes test_codegen_x86_64_passes)
endif ()

if (NOT TARGET test_codegen_x86_64_live_intervals)
    viper_add_test(
            test_codegen_x86_64_live_intervals
            ${VIPER_TESTS_DIR}/unit/test_codegen_x86_64_live_intervals.cpp)
    target_link_libraries(test_codegen_x86_64_live_intervals PRIVATE viper_codegen_x86_64)
    viper_add_ctest(test_codegen_x86_64_live_intervals test_codegen_x86_64_live_intervals)
endif ()

if (NOT TARGET test_codegen_x86_64_spiller)
    viper_add_test(
            test_codegen_x86_64_spiller
            ${VIPER_TESTS_DIR}/unit/test_codegen_x86_64_spiller.cpp)
    target_link_libraries(test_codegen_x86_64_spiller PRIVATE viper_codegen_x86_64)
    viper_add_ctest(test_codegen_x86_64_spiller test_codegen_x86_64_spiller)
endif ()

if (NOT TARGET test_codegen_x86_64_allocator)
    viper_add_test(
            test_codegen_x86_64_allocator
            ${VIPER_TESTS_DIR}/unit/test_codegen_x86_64_allocator.cpp)
    target_link_libraries(test_codegen_x86_64_allocator PRIVATE viper_codegen_x86_64)
    viper_add_ctest(test_codegen_x86_64_allocator test_codegen_x86_64_allocator)
endif ()

if (NOT TARGET test_codegen_x86_64_coalescer)
    viper_add_test(
            test_codegen_x86_64_coalescer
            ${VIPER_TESTS_DIR}/unit/test_codegen_x86_64_coalescer.cpp)
    target_link_libraries(test_codegen_x86_64_coalescer PRIVATE viper_codegen_x86_64)
    viper_add_ctest(test_codegen_x86_64_coalescer test_codegen_x86_64_coalescer)
endif ()

if (NOT TARGET test_codegen_x86_64_lowering_rules)
    viper_add_test(
            test_codegen_x86_64_lowering_rules
            ${VIPER_TESTS_DIR}/unit/test_codegen_x86_64_lowering_rules.cpp)
    target_link_libraries(test_codegen_x86_64_lowering_rules PRIVATE viper_codegen_x86_64)
    viper_add_ctest(test_codegen_x86_64_lowering_rules test_codegen_x86_64_lowering_rules)
endif ()

if (NOT TARGET test_codegen_review_x64_frame)
    viper_add_test(
            test_codegen_review_x64_frame
            ${VIPER_TESTS_DIR}/unit/codegen/test_codegen_review_x64_frame.cpp)
    target_link_libraries(test_codegen_review_x64_frame PRIVATE viper_codegen_x86_64)
    viper_add_ctest(test_codegen_review_x64_frame test_codegen_review_x64_frame)
endif ()

if (NOT TARGET test_codegen_review_fixes_batch2)
    viper_add_test(
            test_codegen_review_fixes_batch2
            ${VIPER_TESTS_DIR}/unit/codegen/test_codegen_review_fixes_batch2.cpp)
    target_link_libraries(test_codegen_review_fixes_batch2 PRIVATE viper_codegen_x86_64)
    viper_add_ctest(test_codegen_review_fixes_batch2 test_codegen_review_fixes_batch2)
endif ()

if (NOT TARGET test_codegen_review_fixes_batch3)
    viper_add_test(
            test_codegen_review_fixes_batch3
            ${VIPER_TESTS_DIR}/unit/codegen/test_codegen_review_fixes_batch3.cpp)
    target_link_libraries(test_codegen_review_fixes_batch3 PRIVATE viper_codegen_x86_64 il_codegen_aarch64)
    viper_add_ctest(test_codegen_review_fixes_batch3 test_codegen_review_fixes_batch3)
endif ()

if (NOT TARGET test_codegen_peephole_optimizations)
    viper_add_test(
            test_codegen_peephole_optimizations
            ${VIPER_TESTS_DIR}/unit/codegen/test_codegen_peephole_optimizations.cpp)
    target_link_libraries(test_codegen_peephole_optimizations PRIVATE il_codegen_aarch64)
    viper_add_ctest(test_codegen_peephole_optimizations test_codegen_peephole_optimizations)
endif ()

if (NOT TARGET test_x86_isel_lea_mul)
    viper_add_test(
            test_x86_isel_lea_mul
            ${VIPER_TESTS_DIR}/unit/codegen/test_x86_isel_lea_mul.cpp)
    target_link_libraries(test_x86_isel_lea_mul PRIVATE viper_codegen_x86_64)
    viper_add_ctest(test_x86_isel_lea_mul test_x86_isel_lea_mul)
endif ()

# Bytecode VM tests
if (NOT TARGET test_bytecode_vm)
    viper_add_test(test_bytecode_vm ${VIPER_TESTS_DIR}/unit/test_bytecode_vm.cpp)
    target_link_libraries(test_bytecode_vm PRIVATE viper_bytecode il_build)
    viper_add_ctest(test_bytecode_vm test_bytecode_vm)
endif ()

function(viper_add_runtime_tests)
    if (TARGET test_rt_string)
        return()
    endif ()

    viper_add_test(test_rt_string ${VIPER_TESTS_DIR}/unit/test_rt_string.cpp)
    target_link_libraries(test_rt_string PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string test_rt_string)

    viper_add_test(test_rt_string_sso ${VIPER_TESTS_DIR}/unit/test_rt_string_sso.cpp)
    target_link_libraries(test_rt_string_sso PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_sso test_rt_string_sso)

    viper_add_test(test_rt_string_invalid ${VIPER_TESTS_DIR}/unit/test_rt_string_invalid.cpp)
    target_link_libraries(test_rt_string_invalid PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_invalid test_rt_string_invalid)
    set_tests_properties(test_rt_string_invalid PROPERTIES WILL_FAIL TRUE)

    viper_add_test(test_rt_conv ${VIPER_TESTS_DIR}/unit/test_rt_conv.cpp)
    target_link_libraries(test_rt_conv PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_conv test_rt_conv)

    viper_add_test(test_rt_int_to_str_big ${VIPER_TESTS_DIR}/unit/test_rt_int_to_str_big.cpp)
    target_link_libraries(test_rt_int_to_str_big PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_int_to_str_big test_rt_int_to_str_big)

    viper_add_test(test_rt_val_str ${VIPER_TESTS_DIR}/runtime/RTValStrTests.cpp)
    target_link_libraries(test_rt_val_str PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_val_str test_rt_val_str)

    viper_add_test(test_rt_math_core ${VIPER_TESTS_DIR}/runtime/RTMathCoreTests.cpp)
    target_link_libraries(test_rt_math_core PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_math_core test_rt_math_core)

    viper_add_test(test_rt_numeric ${VIPER_TESTS_DIR}/runtime/RTNumericTests.cpp)
    target_link_libraries(test_rt_numeric PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_numeric test_rt_numeric)

    viper_add_test(test_rt_float_determinism ${VIPER_TESTS_DIR}/runtime/FloatDeterminismTests.cpp)
    target_link_libraries(test_rt_float_determinism PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_float_determinism test_rt_float_determinism)

    viper_add_test(
            test_rt_float_formatting
            ${VIPER_TESTS_DIR}/runtime/FloatFormattingTests.cpp
            TEST_NAME runtime_FloatFormattingTests)
    target_link_libraries(test_rt_float_formatting PRIVATE ${VIPER_RUNTIME_TEST_LIBS})

    viper_add_test(
            test_rt_int64_to_string
            ${VIPER_TESTS_DIR}/runtime/Int64ToStringTests.cpp
            TEST_NAME runtime_Int64ToStringTests)
    target_link_libraries(test_rt_int64_to_string PRIVATE ${VIPER_RUNTIME_TEST_LIBS})

    viper_add_test(test_rt_string_builder ${VIPER_TESTS_DIR}/runtime/RTStringBuilderTests.c)
    target_link_libraries(test_rt_string_builder PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_builder test_rt_string_builder)

    viper_add_test(test_rt_error_plumbing ${VIPER_TESTS_DIR}/runtime/RtErrorPlumbingTests.cpp)
    target_link_libraries(test_rt_error_plumbing PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_error_plumbing test_rt_error_plumbing)

    viper_add_test(test_rt_errors_io ${VIPER_TESTS_DIR}/runtime/ErrorsIoTests.c)
    target_link_libraries(test_rt_errors_io PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_errors_io test_rt_errors_io)

    viper_add_test(test_rt_file_mode_string ${VIPER_TESTS_DIR}/runtime/RTFileModeStringTests.c)
    target_link_libraries(test_rt_file_mode_string PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_file_mode_string test_rt_file_mode_string)

    viper_add_test(test_rt_file_wrappers ${VIPER_TESTS_DIR}/runtime/FileWrappersTests.c)
    target_link_libraries(test_rt_file_wrappers PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_file_wrappers test_rt_file_wrappers)

    viper_add_test(test_rt_file_channel_io ${VIPER_TESTS_DIR}/runtime/FileChannelIoTests.c)
    target_link_libraries(test_rt_file_channel_io PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_file_channel_io test_rt_file_channel_io)

    viper_add_test(test_rt_file_mode ${VIPER_TESTS_DIR}/runtime/RTFileModeTests.c)
    target_link_libraries(test_rt_file_mode PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_file_mode test_rt_file_mode)

    viper_add_test(test_rt_path ${VIPER_TESTS_DIR}/runtime/RTPathTests.cpp)
    target_link_libraries(test_rt_path PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_path test_rt_path)

    viper_add_test(test_rt_dir ${VIPER_TESTS_DIR}/runtime/RTDirTests.cpp)
    target_link_libraries(test_rt_dir PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_dir test_rt_dir)

    viper_add_test(test_rt_watcher ${VIPER_TESTS_DIR}/runtime/RTWatcherTests.cpp)
    target_link_libraries(test_rt_watcher PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_watcher test_rt_watcher)

    viper_add_test(test_rt_file_ext ${VIPER_TESTS_DIR}/runtime/RTFileExtTests.cpp)
    target_link_libraries(test_rt_file_ext PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_file_ext test_rt_file_ext)

    viper_add_test(test_rt_math_ext ${VIPER_TESTS_DIR}/runtime/RTMathExtTests.cpp)
    target_link_libraries(test_rt_math_ext PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_math_ext test_rt_math_ext)

    viper_add_test(test_rt_clock ${VIPER_TESTS_DIR}/runtime/RTClockTests.cpp)
    target_link_libraries(test_rt_clock PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_clock test_rt_clock)

    viper_add_test(test_rt_stopwatch ${VIPER_TESTS_DIR}/runtime/RTStopwatchTests.cpp)
    target_link_libraries(test_rt_stopwatch PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_stopwatch test_rt_stopwatch)

    viper_add_test(test_rt_guid ${VIPER_TESTS_DIR}/runtime/RTGuidTests.cpp)
    target_link_libraries(test_rt_guid PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_guid test_rt_guid)

    viper_add_test(test_rt_codec ${VIPER_TESTS_DIR}/runtime/RTCodecTests.cpp)
    target_link_libraries(test_rt_codec PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_codec test_rt_codec)

    viper_add_test(test_rt_csv ${VIPER_TESTS_DIR}/runtime/RTCsvTests.cpp)
    target_link_libraries(test_rt_csv PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_csv test_rt_csv)

    viper_add_test(test_rt_json ${VIPER_TESTS_DIR}/runtime/RTJsonTests.cpp)
    target_link_libraries(test_rt_json PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_json test_rt_json)

    viper_add_test(test_rt_json_stream ${VIPER_TESTS_DIR}/runtime/RTJsonStreamTests.cpp)
    target_link_libraries(test_rt_json_stream PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_json_stream test_rt_json_stream)

    viper_add_test(test_rt_regex ${VIPER_TESTS_DIR}/runtime/RTRegexTests.cpp)
    target_link_libraries(test_rt_regex PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_regex test_rt_regex)

    viper_add_test(test_rt_datetime ${VIPER_TESTS_DIR}/runtime/RTDatetimeTests.cpp)
    target_link_libraries(test_rt_datetime PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_datetime test_rt_datetime)

    viper_add_test(test_rt_aes ${VIPER_TESTS_DIR}/runtime/RTAesTests.cpp)
    target_link_libraries(test_rt_aes PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_aes test_rt_aes)

    viper_add_test(test_rt_bigint ${VIPER_TESTS_DIR}/runtime/RTBigintTests.cpp)
    target_link_libraries(test_rt_bigint PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_bigint test_rt_bigint)

    viper_add_test(test_rt_xml ${VIPER_TESTS_DIR}/runtime/RTXmlTests.cpp)
    target_link_libraries(test_rt_xml PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_xml test_rt_xml)

    viper_add_test(test_rt_hash ${VIPER_TESTS_DIR}/runtime/RTHashTests.cpp)
    target_link_libraries(test_rt_hash PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_hash test_rt_hash)

    viper_add_test(test_rt_crypto ${VIPER_TESTS_DIR}/runtime/RTCryptoTests.cpp)
    target_link_libraries(test_rt_crypto PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_crypto test_rt_crypto)

    viper_add_test(test_rt_cipher ${VIPER_TESTS_DIR}/runtime/RTCipherTests.cpp)
    target_link_libraries(test_rt_cipher PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_cipher test_rt_cipher)

    viper_add_test(test_rt_stream ${VIPER_TESTS_DIR}/runtime/RTStreamTests.cpp)
    target_link_libraries(test_rt_stream PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_stream test_rt_stream)

    viper_add_test(test_rt_seq_functional ${VIPER_TESTS_DIR}/runtime/RTSeqFunctionalTests.cpp)
    target_link_libraries(test_rt_seq_functional PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_seq_functional test_rt_seq_functional)

    viper_add_test(test_rt_password ${VIPER_TESTS_DIR}/runtime/RTPasswordTests.cpp)
    target_link_libraries(test_rt_password PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_password test_rt_password)

    viper_add_test(test_rt_duration ${VIPER_TESTS_DIR}/runtime/RTDurationTests.cpp)
    target_link_libraries(test_rt_duration PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_duration test_rt_duration)

    viper_add_test(test_rt_glob ${VIPER_TESTS_DIR}/runtime/RTGlobTests.cpp)
    target_link_libraries(test_rt_glob PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_glob test_rt_glob)

    viper_add_test(test_rt_tempfile ${VIPER_TESTS_DIR}/runtime/RTTempFileTests.cpp)
    target_link_libraries(test_rt_tempfile PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_tempfile test_rt_tempfile)

    viper_add_test(test_rt_result ${VIPER_TESTS_DIR}/runtime/RTResultTests.cpp)
    target_link_libraries(test_rt_result PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_result test_rt_result)

    viper_add_test(test_rt_option ${VIPER_TESTS_DIR}/runtime/RTOptionTests.cpp)
    target_link_libraries(test_rt_option PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_option test_rt_option)

    viper_add_test(test_rt_deque ${VIPER_TESTS_DIR}/runtime/RTDequeTests.cpp)
    target_link_libraries(test_rt_deque PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_deque test_rt_deque)

    viper_add_test(test_rt_convert_coll ${VIPER_TESTS_DIR}/runtime/RTConvertCollTests.cpp)
    target_link_libraries(test_rt_convert_coll PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_convert_coll test_rt_convert_coll)

    viper_add_test(test_rt_set ${VIPER_TESTS_DIR}/runtime/RTSetTests.cpp)
    target_link_libraries(test_rt_set PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_set test_rt_set)

    viper_add_test(test_rt_seq_box ${VIPER_TESTS_DIR}/runtime/RTSeqBoxTests.cpp)
    target_link_libraries(test_rt_seq_box PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_seq_box test_rt_seq_box)

    viper_add_test(test_rt_list_box ${VIPER_TESTS_DIR}/runtime/RTListBoxTests.cpp)
    target_link_libraries(test_rt_list_box PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_list_box test_rt_list_box)

    viper_add_test(test_rt_sortedset ${VIPER_TESTS_DIR}/runtime/RTSortedSetTests.cpp)
    target_link_libraries(test_rt_sortedset PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_sortedset test_rt_sortedset)

    viper_add_test(test_rt_scanner ${VIPER_TESTS_DIR}/runtime/RTScannerTests.cpp)
    target_link_libraries(test_rt_scanner PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_scanner test_rt_scanner)

    viper_add_test(test_rt_dateonly ${VIPER_TESTS_DIR}/runtime/RTDateOnlyTests.cpp)
    target_link_libraries(test_rt_dateonly PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_dateonly test_rt_dateonly)

    viper_add_test(test_rt_textwrap ${VIPER_TESTS_DIR}/runtime/RTTextWrapTests.cpp)
    target_link_libraries(test_rt_textwrap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_textwrap test_rt_textwrap)

    viper_add_test(test_rt_lazy ${VIPER_TESTS_DIR}/runtime/RTLazyTests.cpp)
    target_link_libraries(test_rt_lazy PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_lazy test_rt_lazy)

    viper_add_test(test_rt_lazyseq ${VIPER_TESTS_DIR}/runtime/RTLazySeqTests.cpp)
    target_link_libraries(test_rt_lazyseq PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_lazyseq test_rt_lazyseq)

    viper_add_test(test_rt_pattern ${VIPER_TESTS_DIR}/runtime/RTPatternTests.cpp)
    target_link_libraries(test_rt_pattern PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_pattern test_rt_pattern)

    viper_add_test(test_rt_compiled_pattern ${VIPER_TESTS_DIR}/runtime/RTCompiledPatternTests.cpp)
    target_link_libraries(test_rt_compiled_pattern PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_compiled_pattern test_rt_compiled_pattern)

    viper_add_test(test_rt_template ${VIPER_TESTS_DIR}/runtime/RTTemplateTests.cpp)
    target_link_libraries(test_rt_template PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_template test_rt_template)

    viper_add_test(test_rt_compress ${VIPER_TESTS_DIR}/runtime/RTCompressTests.cpp)
    target_link_libraries(test_rt_compress PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_compress test_rt_compress)

    viper_add_test(test_rt_archive ${VIPER_TESTS_DIR}/runtime/RTArchiveTests.cpp)
    target_link_libraries(test_rt_archive PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_archive test_rt_archive)

    viper_add_test(test_rt_canvas_window ${VIPER_TESTS_DIR}/runtime/RTCanvasWindowTests.cpp)
    target_link_libraries(test_rt_canvas_window PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_canvas_window test_rt_canvas_window)

    viper_add_test(test_rt_network ${VIPER_TESTS_DIR}/runtime/RTNetworkTests.cpp)
    target_link_libraries(test_rt_network PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_network test_rt_network)

    viper_add_test(test_rt_network_timeout ${VIPER_TESTS_DIR}/runtime/RTNetworkTimeoutTests.cpp)
    target_link_libraries(test_rt_network_timeout PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_network_timeout test_rt_network_timeout)

    viper_add_test(test_rt_websocket ${VIPER_TESTS_DIR}/runtime/RTWebSocketTests.cpp)
    target_link_libraries(test_rt_websocket PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_websocket test_rt_websocket)

    viper_add_test(test_rt_tls_cert ${VIPER_TESTS_DIR}/runtime/RTTlsCertTests.cpp)
    target_link_libraries(test_rt_tls_cert PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_tls_cert test_rt_tls_cert)

    viper_add_test(test_rt_restclient ${VIPER_TESTS_DIR}/runtime/RTRestClientTests.cpp)
    target_link_libraries(test_rt_restclient PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_restclient test_rt_restclient)

    viper_add_test(test_rt_bag ${VIPER_TESTS_DIR}/runtime/RTBagTests.cpp)
    target_link_libraries(test_rt_bag PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_bag test_rt_bag)

    viper_add_test(test_rt_binfile ${VIPER_TESTS_DIR}/runtime/RTBinFileTests.cpp)
    target_link_libraries(test_rt_binfile PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_binfile test_rt_binfile)

    viper_add_test(test_rt_memstream ${VIPER_TESTS_DIR}/runtime/RTMemStreamTests.cpp)
    target_link_libraries(test_rt_memstream PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_memstream test_rt_memstream)

    viper_add_test(test_rt_linereader ${VIPER_TESTS_DIR}/runtime/RTLineReaderTests.cpp)
    target_link_libraries(test_rt_linereader PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_linereader test_rt_linereader)

    viper_add_test(test_rt_linewriter ${VIPER_TESTS_DIR}/runtime/RTLineWriterTests.cpp)
    target_link_libraries(test_rt_linewriter PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_linewriter test_rt_linewriter)

    viper_add_test(test_rt_disk_full ${VIPER_TESTS_DIR}/runtime/RTDiskFullTests.cpp)
    target_link_libraries(test_rt_disk_full PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_disk_full test_rt_disk_full)

    viper_add_test(test_rt_canvas_unavailable ${VIPER_TESTS_DIR}/runtime/RTCanvasUnavailableTests.cpp)
    target_link_libraries(test_rt_canvas_unavailable PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_canvas_unavailable test_rt_canvas_unavailable)

    viper_add_test(test_rt_file_exhaustion ${VIPER_TESTS_DIR}/runtime/RTFileExhaustionTests.cpp)
    target_link_libraries(test_rt_file_exhaustion PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_file_exhaustion test_rt_file_exhaustion)

    viper_add_test(test_rt_exec ${VIPER_TESTS_DIR}/runtime/RTExecTests.cpp)
    target_link_libraries(test_rt_exec PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_exec test_rt_exec)

    viper_add_test(test_rt_machine ${VIPER_TESTS_DIR}/runtime/RTMachineTests.cpp)
    target_link_libraries(test_rt_machine PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_machine test_rt_machine)

    viper_add_test(test_rt_bits ${VIPER_TESTS_DIR}/runtime/RTBitsTests.cpp)
    target_link_libraries(test_rt_bits PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_bits test_rt_bits)

    viper_add_test(test_rt_diag ${VIPER_TESTS_DIR}/runtime/RTDiagTests.cpp)
    target_link_libraries(test_rt_diag PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_diag test_rt_diag)

    viper_add_test(test_rt_fmt ${VIPER_TESTS_DIR}/runtime/RTFmtTests.cpp)
    target_link_libraries(test_rt_fmt PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_fmt test_rt_fmt)

    viper_add_test(test_rt_log ${VIPER_TESTS_DIR}/runtime/RTLogTests.cpp)
    target_link_libraries(test_rt_log PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_log test_rt_log)

    viper_add_test(test_rt_countdown ${VIPER_TESTS_DIR}/runtime/RTCountdownTests.cpp)
    target_link_libraries(test_rt_countdown PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_countdown test_rt_countdown)

    viper_add_test(test_rt_parse ${VIPER_TESTS_DIR}/runtime/RTParseTests.cpp)
    target_link_libraries(test_rt_parse PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_parse test_rt_parse)

    viper_add_test(test_rt_bool_std ${VIPER_TESTS_DIR}/runtime/RTBoolStdTests.cpp)
    target_link_libraries(test_rt_bool_std PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_bool_std test_rt_bool_std)

    viper_add_test(test_rt_vec2 ${VIPER_TESTS_DIR}/runtime/RTVec2Tests.cpp)
    target_link_libraries(test_rt_vec2 PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_vec2 test_rt_vec2)

    viper_add_test(test_rt_vec3 ${VIPER_TESTS_DIR}/runtime/RTVec3Tests.cpp)
    target_link_libraries(test_rt_vec3 PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_vec3 test_rt_vec3)

    viper_add_test(test_rt_quat ${VIPER_TESTS_DIR}/runtime/RTQuatTests.cpp)
    target_link_libraries(test_rt_quat PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_quat test_rt_quat)

    viper_add_test(test_rt_spline ${VIPER_TESTS_DIR}/runtime/RTSplineTests.cpp)
    target_link_libraries(test_rt_spline PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_spline test_rt_spline)

    viper_add_test(test_rt_pixels ${VIPER_TESTS_DIR}/runtime/RTPixelsTests.cpp)
    target_link_libraries(test_rt_pixels PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_pixels test_rt_pixels)

    viper_add_test(test_rt_pixels_draw ${VIPER_TESTS_DIR}/runtime/RTPixelsDrawTests.cpp)
    target_link_libraries(test_rt_pixels_draw PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_pixels_draw test_rt_pixels_draw)

    viper_add_test(test_rt_scene ${VIPER_TESTS_DIR}/runtime/RTSceneTests.cpp)
    target_link_libraries(test_rt_scene PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_scene test_rt_scene)

    viper_add_test(test_rt_spritebatch ${VIPER_TESTS_DIR}/runtime/RTSpriteBatchTests.cpp)
    target_link_libraries(test_rt_spritebatch PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_spritebatch test_rt_spritebatch)

    viper_add_test(test_rt_listview ${VIPER_TESTS_DIR}/runtime/RTListViewTests.cpp)
    target_link_libraries(test_rt_listview PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_listview test_rt_listview)

    viper_add_test(test_rt_canvas_ext ${VIPER_TESTS_DIR}/runtime/RTCanvasExtTests.cpp)
    target_link_libraries(test_rt_canvas_ext PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_canvas_ext test_rt_canvas_ext)

    viper_add_test(test_rt_keyboard ${VIPER_TESTS_DIR}/runtime/RTKeyboardTests.cpp)
    target_link_libraries(test_rt_keyboard PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_keyboard test_rt_keyboard)

    viper_add_test(test_rt_mouse ${VIPER_TESTS_DIR}/runtime/RTMouseTests.cpp)
    target_link_libraries(test_rt_mouse PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_mouse test_rt_mouse)

    viper_add_test(test_rt_pad ${VIPER_TESTS_DIR}/runtime/RTPadTests.cpp)
    target_link_libraries(test_rt_pad PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_pad test_rt_pad)

    viper_add_test(test_rt_abs_i64_overflow ${VIPER_TESTS_DIR}/runtime/RTAbsI64OverflowTests.cpp)
    target_link_libraries(test_rt_abs_i64_overflow PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_abs_i64_overflow test_rt_abs_i64_overflow)

    viper_add_test(test_rt_random ${VIPER_TESTS_DIR}/runtime/RTRandomTests.cpp)
    target_link_libraries(test_rt_random PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_random test_rt_random)

    viper_add_test(test_rt_instr ${VIPER_TESTS_DIR}/runtime/RTInstrTests.cpp)
    target_link_libraries(test_rt_instr PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_instr test_rt_instr)

    viper_add_test(test_rt_term_color ${VIPER_TESTS_DIR}/runtime/RTTermColorTests.cpp)
    target_link_libraries(test_rt_term_color PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    if (APPLE)
        target_link_libraries(test_rt_term_color PRIVATE util)
    endif ()
    viper_add_ctest(test_rt_term_color test_rt_term_color)

    viper_add_test(test_rt_trim_case ${VIPER_TESTS_DIR}/runtime/RTTrimCaseTests.cpp)
    target_link_libraries(test_rt_trim_case PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_trim_case test_rt_trim_case)

    viper_add_test(test_rt_chr_asc ${VIPER_TESTS_DIR}/runtime/RTChrAscTests.cpp)
    target_link_libraries(test_rt_chr_asc PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_chr_asc test_rt_chr_asc)

    viper_add_test(test_rt_chr_invalid ${VIPER_TESTS_DIR}/runtime/RTChrInvalidTests.cpp)
    target_link_libraries(test_rt_chr_invalid PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_chr_invalid test_rt_chr_invalid)

    # rt_args tests
    viper_add_test(test_rt_args ${VIPER_TESTS_DIR}/runtime/RTArgsTests.cpp)
    target_link_libraries(test_rt_args PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_args test_rt_args)

    viper_add_test(test_rt_args_trap ${VIPER_TESTS_DIR}/runtime/RTArgsTrapTests.cpp)
    target_link_libraries(test_rt_args_trap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_args_trap test_rt_args_trap)

    # Sleep runtime test
    viper_add_test(test_rt_sleep ${VIPER_TESTS_DIR}/runtime/RT_SleepTests.cpp)
    target_link_libraries(test_rt_sleep PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_sleep test_rt_sleep)

    # Timer runtime test
    viper_add_test(test_rt_timer ${VIPER_TESTS_DIR}/runtime/RT_TimerTests.cpp)
    target_link_libraries(test_rt_timer PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_timer test_rt_timer)

    # Threading runtime tests (Monitor/Thread/SafeI64)
    viper_add_test(test_rt_threads_monitor ${VIPER_TESTS_DIR}/runtime/RTThreadsMonitorTests.cpp)
    target_link_libraries(test_rt_threads_monitor PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_threads_monitor test_rt_threads_monitor)

    viper_add_test(test_rt_threads_thread ${VIPER_TESTS_DIR}/runtime/RTThreadsThreadTests.cpp)
    target_link_libraries(test_rt_threads_thread PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_threads_thread test_rt_threads_thread)

    viper_add_test(test_rt_threads_primitives ${VIPER_TESTS_DIR}/runtime/RTThreadsPrimitivesTests.cpp)
    target_link_libraries(test_rt_threads_primitives PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_threads_primitives test_rt_threads_primitives)

    viper_add_test(test_rt_future ${VIPER_TESTS_DIR}/runtime/RTFutureTests.cpp)
    target_link_libraries(test_rt_future PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_future test_rt_future)

    viper_add_test(test_rt_parallel ${VIPER_TESTS_DIR}/runtime/RTParallelTests.cpp)
    target_link_libraries(test_rt_parallel PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_parallel test_rt_parallel)

    viper_add_test(test_rt_parallel_reduce ${VIPER_TESTS_DIR}/runtime/RTParallelReduceTests.cpp)
    target_link_libraries(test_rt_parallel_reduce PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_parallel_reduce test_rt_parallel_reduce)

    viper_add_test(test_rt_concmap ${VIPER_TESTS_DIR}/runtime/RTConcMapTests.cpp)
    target_link_libraries(test_rt_concmap PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_concmap test_rt_concmap)

    viper_add_test(test_rt_channel ${VIPER_TESTS_DIR}/runtime/RTChannelTests.cpp)
    target_link_libraries(test_rt_channel PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_channel test_rt_channel)

    viper_add_test(test_rt_threadpool ${VIPER_TESTS_DIR}/runtime/RTThreadPoolTests.cpp)
    target_link_libraries(test_rt_threadpool PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_threadpool test_rt_threadpool)

    viper_add_test(test_rt_async ${VIPER_TESTS_DIR}/runtime/RTAsyncTests.cpp)
    target_link_libraries(test_rt_async PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_async test_rt_async)

    viper_add_test(test_rt_serialize ${VIPER_TESTS_DIR}/runtime/RTSerializeTests.cpp)
    target_link_libraries(test_rt_serialize PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_serialize test_rt_serialize)

    viper_add_test(test_rt_gc ${VIPER_TESTS_DIR}/runtime/RTGCTests.cpp)
    target_link_libraries(test_rt_gc PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_gc test_rt_gc)

    viper_add_test(test_rt_gc_hash ${VIPER_TESTS_DIR}/runtime/RTGCHashTableTests.cpp)
    target_link_libraries(test_rt_gc_hash PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_gc_hash test_rt_gc_hash)

    viper_add_test(test_rt_viperdos_platform ${VIPER_TESTS_DIR}/runtime/RTViperDOSPlatformTests.cpp)
    target_link_libraries(test_rt_viperdos_platform PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_viperdos_platform test_rt_viperdos_platform)

    viper_add_test(test_rt_string_size_clamp ${VIPER_TESTS_DIR}/runtime/RTStringSizeClampTests.cpp)
    target_link_libraries(test_rt_string_size_clamp PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_size_clamp test_rt_string_size_clamp)

    viper_add_test(test_rt_string_ranges ${VIPER_TESTS_DIR}/runtime/RTStringRangeTests.cpp)
    target_link_libraries(test_rt_string_ranges PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_ranges test_rt_string_ranges)

    viper_add_test(test_rt_alloc ${VIPER_TESTS_DIR}/runtime/RTAllocTests.cpp)
    target_link_libraries(test_rt_alloc PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_alloc test_rt_alloc)

    viper_add_test(test_rt_alloc_oom ${VIPER_TESTS_DIR}/runtime/RTAllocOOMTests.cpp)
    target_link_libraries(test_rt_alloc_oom PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_alloc_oom test_rt_alloc_oom)

    viper_add_test(
            test_rt_string_alloc_overflow
            ${VIPER_TESTS_DIR}/runtime/RTStringAllocOverflowTests.cpp)
    target_link_libraries(test_rt_string_alloc_overflow PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_alloc_overflow test_rt_string_alloc_overflow)

    viper_add_test(
            test_rt_string_len_clamp
            ${VIPER_TESTS_DIR}/runtime/RTStringLenClampTests.cpp)
    target_link_libraries(test_rt_string_len_clamp PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_len_clamp test_rt_string_len_clamp)

    viper_add_test(
            test_rt_string_wrap_alloc_fail
            ${VIPER_TESTS_DIR}/runtime/RTStringWrapAllocFailTests.cpp)
    target_link_libraries(test_rt_string_wrap_alloc_fail PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_wrap_alloc_fail test_rt_string_wrap_alloc_fail)

    viper_add_test(test_rt_alloc_zero ${VIPER_TESTS_DIR}/runtime/RTAllocZeroTests.cpp)
    target_link_libraries(test_rt_alloc_zero PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_alloc_zero test_rt_alloc_zero)

    viper_add_test(test_rt_alloc_zero_fill ${VIPER_TESTS_DIR}/runtime/RTAllocZeroFillTests.cpp)
    target_link_libraries(test_rt_alloc_zero_fill PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_alloc_zero_fill test_rt_alloc_zero_fill)

    viper_add_test(test_rt_object_alloc ${VIPER_TESTS_DIR}/runtime/RTObjectAllocTests.cpp)
    target_link_libraries(test_rt_object_alloc PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_object_alloc test_rt_object_alloc)

    viper_add_test(test_rt_oop_dispatch ${VIPER_TESTS_DIR}/runtime/RTOOPDispatchTests.cpp)
    target_link_libraries(test_rt_oop_dispatch PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_oop_dispatch test_rt_oop_dispatch)

    viper_add_test(test_rt_type_registry ${VIPER_TESTS_DIR}/runtime/RTTypeRegistryTests.cpp)
    target_link_libraries(test_rt_type_registry PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_type_registry test_rt_type_registry)

    viper_add_test(
            test_rt_type_registry_context_isolation
            ${VIPER_TESTS_DIR}/runtime/RTTypeRegistryContextIsolationTests.cpp)
    target_link_libraries(test_rt_type_registry_context_isolation PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_type_registry_context_isolation test_rt_type_registry_context_isolation)

    viper_add_test(test_rt_array_i32 ${VIPER_TESTS_DIR}/runtime/RTArrayI32Tests.cpp)
    target_link_libraries(test_rt_array_i32 PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_array_i32 test_rt_array_i32)

    viper_add_test(test_rt_array_i32_refcount ${VIPER_TESTS_DIR}/runtime/RTArrayI32RefcountTests.cpp)
    target_link_libraries(test_rt_array_i32_refcount PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_array_i32_refcount test_rt_array_i32_refcount)

    viper_add_test(test_rt_array_str ${VIPER_TESTS_DIR}/runtime/RTArrayStrTests.cpp)
    target_link_libraries(test_rt_array_str PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_array_str test_rt_array_str)

    viper_add_test(test_rt_input_line ${VIPER_TESTS_DIR}/runtime/RTInputLineTests.cpp)
    target_link_libraries(test_rt_input_line PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_input_line test_rt_input_line)

    viper_add_test(test_rt_input_overflow ${VIPER_TESTS_DIR}/unit/runtime/RTInputGrowOverflowTests.cpp)
    target_link_libraries(test_rt_input_overflow PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_input_overflow test_rt_input_overflow)

    viper_add_test(
            test_rt_file_read_line_overflow
            ${VIPER_TESTS_DIR}/unit/runtime/RTFileReadLineOverflowTests.cpp)
    target_link_libraries(test_rt_file_read_line_overflow PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_file_read_line_overflow test_rt_file_read_line_overflow)

    viper_add_test(test_rt_action_mapping ${VIPER_TESTS_DIR}/unit/runtime/RTActionMappingTests.cpp)
    target_link_libraries(test_rt_action_mapping PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_action_mapping test_rt_action_mapping)

    viper_add_test(test_rt_keychord ${VIPER_TESTS_DIR}/runtime/RTKeyChordTests.cpp)
    target_link_libraries(test_rt_keychord PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_keychord test_rt_keychord)

    viper_add_test(test_rt_action_persist ${VIPER_TESTS_DIR}/runtime/RTActionPersistTests.cpp)
    target_link_libraries(test_rt_action_persist PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_action_persist test_rt_action_persist)

    viper_add_test(test_rt_grid2d ${VIPER_TESTS_DIR}/unit/runtime/RTGrid2DTests.cpp)
    target_link_libraries(test_rt_grid2d PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_grid2d test_rt_grid2d)

    viper_add_test(test_rt_frame_timer ${VIPER_TESTS_DIR}/unit/runtime/RTTimerTests.cpp)
    target_link_libraries(test_rt_frame_timer PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_frame_timer test_rt_frame_timer)

    viper_add_test(test_rt_input_line_fail ${VIPER_TESTS_DIR}/runtime/RTInputLineFailTests.cpp)
    target_link_libraries(test_rt_input_line_fail PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_input_line_fail test_rt_input_line_fail)

    viper_add_test(test_rt_input_numeric_fail ${VIPER_TESTS_DIR}/runtime/RTInputNumericFailTests.cpp)
    target_link_libraries(test_rt_input_numeric_fail PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_input_numeric_fail test_rt_input_numeric_fail)

    # Basic SLEEP lowering test
    if (NOT TARGET test_basic_sleep_lowering)
        viper_add_test(test_basic_sleep_lowering ${VIPER_TESTS_DIR}/unit/Basic_Sleep_Lowering.cpp)
        target_link_libraries(test_basic_sleep_lowering PRIVATE fe_basic)
        viper_add_ctest(test_basic_sleep_lowering test_basic_sleep_lowering)
    endif ()

    # Basic TIMER lowering test
    if (NOT TARGET test_basic_timer_lowering)
        viper_add_test(test_basic_timer_lowering ${VIPER_TESTS_DIR}/unit/Basic_Timer_Lowering.cpp)
        target_link_libraries(test_basic_timer_lowering PRIVATE fe_basic)
        viper_add_ctest(test_basic_timer_lowering test_basic_timer_lowering)
    endif ()

    viper_add_test(test_rt_split_fields ${VIPER_TESTS_DIR}/runtime/RTSplitFieldsTests.cpp)
    target_link_libraries(test_rt_split_fields PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_split_fields test_rt_split_fields)

    viper_add_test(test_rt_seq ${VIPER_TESTS_DIR}/runtime/RTSeqTests.cpp)
    target_link_libraries(test_rt_seq PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_seq test_rt_seq)

    viper_add_test(test_rt_list ${VIPER_TESTS_DIR}/runtime/RTListTests.cpp)
    target_link_libraries(test_rt_list PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_list test_rt_list)

    viper_add_test(test_rt_stack ${VIPER_TESTS_DIR}/runtime/RTStackTests.cpp)
    target_link_libraries(test_rt_stack PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_stack test_rt_stack)

    viper_add_test(test_rt_queue ${VIPER_TESTS_DIR}/runtime/RTQueueTests.cpp)
    target_link_libraries(test_rt_queue PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_queue test_rt_queue)

    viper_add_test(test_rt_ring ${VIPER_TESTS_DIR}/runtime/RTRingTests.cpp)
    target_link_libraries(test_rt_ring PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_ring test_rt_ring)

    viper_add_test(test_rt_bitset ${VIPER_TESTS_DIR}/runtime/RTBitSetTests.cpp)
    target_link_libraries(test_rt_bitset PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_bitset test_rt_bitset)

    viper_add_test(test_rt_lrucache ${VIPER_TESTS_DIR}/runtime/RTLruCacheTests.cpp)
    target_link_libraries(test_rt_lrucache PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_lrucache test_rt_lrucache)

    viper_add_test(test_rt_map ${VIPER_TESTS_DIR}/runtime/RTMapTests.cpp)
    target_link_libraries(test_rt_map PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_map test_rt_map)

    viper_add_test(test_rt_multimap ${VIPER_TESTS_DIR}/runtime/RTMultiMapTests.cpp)
    target_link_libraries(test_rt_multimap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_multimap test_rt_multimap)

    viper_add_test(test_rt_treemap ${VIPER_TESTS_DIR}/runtime/RTTreeMapTests.cpp)
    target_link_libraries(test_rt_treemap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_treemap test_rt_treemap)

    viper_add_test(test_rt_trie ${VIPER_TESTS_DIR}/runtime/RTTrieTests.cpp)
    target_link_libraries(test_rt_trie PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_trie test_rt_trie)

    viper_add_test(test_rt_modvar ${VIPER_TESTS_DIR}/runtime/RTModvarTests.cpp)
    target_link_libraries(test_rt_modvar PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_modvar test_rt_modvar)

    viper_add_test(test_rt_bytes ${VIPER_TESTS_DIR}/runtime/RTBytesTests.cpp)
    target_link_libraries(test_rt_bytes PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_bytes test_rt_bytes)

    viper_add_test(test_rt_string_ext ${VIPER_TESTS_DIR}/runtime/RTStringExtTests.cpp)
    target_link_libraries(test_rt_string_ext PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_ext test_rt_string_ext)

    viper_add_test(test_rt_string_edge ${VIPER_TESTS_DIR}/runtime/RTStringEdgeTests.cpp)
    target_link_libraries(test_rt_string_edge PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_edge test_rt_string_edge)

    # Game development abstractions tests
    viper_add_test(test_rt_statemachine ${VIPER_TESTS_DIR}/unit/runtime/RTStateMachineTests.cpp)
    target_link_libraries(test_rt_statemachine PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_statemachine test_rt_statemachine)

    viper_add_test(test_rt_tween ${VIPER_TESTS_DIR}/unit/runtime/RTTweenTests.cpp)
    target_link_libraries(test_rt_tween PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_tween test_rt_tween)

    viper_add_test(test_rt_buttongroup ${VIPER_TESTS_DIR}/unit/runtime/RTButtonGroupTests.cpp)
    target_link_libraries(test_rt_buttongroup PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_buttongroup test_rt_buttongroup)

    viper_add_test(test_rt_camera ${VIPER_TESTS_DIR}/unit/runtime/RTCameraTests.cpp)
    target_link_libraries(test_rt_camera PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_camera test_rt_camera)

    viper_add_test(test_rt_smoothvalue ${VIPER_TESTS_DIR}/unit/runtime/RTSmoothValueTests.cpp)
    target_link_libraries(test_rt_smoothvalue PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_smoothvalue test_rt_smoothvalue)

    viper_add_test(test_rt_particle ${VIPER_TESTS_DIR}/unit/runtime/RTParticleTests.cpp)
    target_link_libraries(test_rt_particle PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_particle test_rt_particle)

    viper_add_test(test_rt_spriteanim ${VIPER_TESTS_DIR}/unit/runtime/RTSpriteAnimTests.cpp)
    target_link_libraries(test_rt_spriteanim PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_spriteanim test_rt_spriteanim)

    viper_add_test(test_rt_collision ${VIPER_TESTS_DIR}/unit/runtime/RTCollisionTests.cpp)
    target_link_libraries(test_rt_collision PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_collision test_rt_collision)

    viper_add_test(test_rt_objpool ${VIPER_TESTS_DIR}/unit/runtime/RTObjPoolTests.cpp)
    target_link_libraries(test_rt_objpool PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_objpool test_rt_objpool)

    viper_add_test(test_rt_screenfx ${VIPER_TESTS_DIR}/unit/runtime/RTScreenFXTests.cpp)
    target_link_libraries(test_rt_screenfx PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_screenfx test_rt_screenfx)

    viper_add_test(test_rt_pathfollow ${VIPER_TESTS_DIR}/unit/runtime/RTPathFollowTests.cpp)
    target_link_libraries(test_rt_pathfollow PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_pathfollow test_rt_pathfollow)

    viper_add_test(test_rt_quadtree ${VIPER_TESTS_DIR}/unit/runtime/RTQuadtreeTests.cpp)
    target_link_libraries(test_rt_quadtree PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_quadtree test_rt_quadtree)

    viper_add_test(test_rt_perlin ${VIPER_TESTS_DIR}/runtime/RTPerlinTests.cpp)
    target_link_libraries(test_rt_perlin PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_perlin test_rt_perlin)

    viper_add_test(test_rt_easing ${VIPER_TESTS_DIR}/runtime/RTEasingTests.cpp)
    target_link_libraries(test_rt_easing PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_easing test_rt_easing)

    viper_add_test(test_rt_color_utils ${VIPER_TESTS_DIR}/runtime/RTColorUtilsTests.cpp)
    target_link_libraries(test_rt_color_utils PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_color_utils test_rt_color_utils)

    viper_add_test(test_rt_ini ${VIPER_TESTS_DIR}/runtime/RTIniTests.cpp)
    target_link_libraries(test_rt_ini PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_ini test_rt_ini)

    viper_add_test(test_rt_fmt_ext ${VIPER_TESTS_DIR}/runtime/RTFmtExtTests.cpp)
    target_link_libraries(test_rt_fmt_ext PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_fmt_ext test_rt_fmt_ext)

    viper_add_test(test_rt_case_convert ${VIPER_TESTS_DIR}/runtime/RTCaseConvertTests.cpp)
    target_link_libraries(test_rt_case_convert PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_case_convert test_rt_case_convert)

    viper_add_test(test_rt_bimap ${VIPER_TESTS_DIR}/runtime/RTBiMapTests.cpp)
    target_link_libraries(test_rt_bimap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_bimap test_rt_bimap)

    viper_add_test(test_rt_countmap ${VIPER_TESTS_DIR}/runtime/RTCountMapTests.cpp)
    target_link_libraries(test_rt_countmap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_countmap test_rt_countmap)

    viper_add_test(test_rt_pluralize ${VIPER_TESTS_DIR}/runtime/RTPluralizeTests.cpp)
    target_link_libraries(test_rt_pluralize PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_pluralize test_rt_pluralize)

    viper_add_test(test_rt_version ${VIPER_TESTS_DIR}/runtime/RTVersionTests.cpp)
    target_link_libraries(test_rt_version PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_version test_rt_version)

    viper_add_test(test_rt_reltime ${VIPER_TESTS_DIR}/runtime/RTRelTimeTests.cpp)
    target_link_libraries(test_rt_reltime PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_reltime test_rt_reltime)

    viper_add_test(test_rt_daterange ${VIPER_TESTS_DIR}/runtime/RTDateRangeTests.cpp)
    target_link_libraries(test_rt_daterange PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_daterange test_rt_daterange)

    viper_add_test(test_rt_numfmt ${VIPER_TESTS_DIR}/runtime/RTNumFmtTests.cpp)
    target_link_libraries(test_rt_numfmt PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_numfmt test_rt_numfmt)

    viper_add_test(test_rt_bloomfilter ${VIPER_TESTS_DIR}/runtime/RTBloomFilterTests.cpp)
    target_link_libraries(test_rt_bloomfilter PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_bloomfilter test_rt_bloomfilter)

    viper_add_test(test_rt_unionfind ${VIPER_TESTS_DIR}/runtime/RTUnionFindTests.cpp)
    target_link_libraries(test_rt_unionfind PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_unionfind test_rt_unionfind)

    viper_add_test(test_rt_orderedmap ${VIPER_TESTS_DIR}/runtime/RTOrderedMapTests.cpp)
    target_link_libraries(test_rt_orderedmap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_orderedmap test_rt_orderedmap)

    viper_add_test(test_rt_diff ${VIPER_TESTS_DIR}/runtime/RTDiffTests.cpp)
    target_link_libraries(test_rt_diff PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_diff test_rt_diff)

    viper_add_test(test_rt_defaultmap ${VIPER_TESTS_DIR}/runtime/RTDefaultMapTests.cpp)
    target_link_libraries(test_rt_defaultmap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_defaultmap test_rt_defaultmap)

    viper_add_test(test_rt_frozenset ${VIPER_TESTS_DIR}/runtime/RTFrozenSetTests.cpp)
    target_link_libraries(test_rt_frozenset PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_frozenset test_rt_frozenset)

    viper_add_test(test_rt_frozenmap ${VIPER_TESTS_DIR}/runtime/RTFrozenMapTests.cpp)
    target_link_libraries(test_rt_frozenmap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_frozenmap test_rt_frozenmap)

    viper_add_test(test_rt_sparsearray ${VIPER_TESTS_DIR}/runtime/RTSparseArrayTests.cpp)
    target_link_libraries(test_rt_sparsearray PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_sparsearray test_rt_sparsearray)

    viper_add_test(test_rt_concqueue ${VIPER_TESTS_DIR}/runtime/RTConcQueueTests.cpp)
    target_link_libraries(test_rt_concqueue PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_concqueue test_rt_concqueue)

    viper_add_test(test_rt_msgbus ${VIPER_TESTS_DIR}/runtime/RTMsgBusTests.cpp)
    target_link_libraries(test_rt_msgbus PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_msgbus test_rt_msgbus)

    viper_add_test(test_rt_toml ${VIPER_TESTS_DIR}/runtime/RTTomlTests.cpp)
    target_link_libraries(test_rt_toml PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_toml test_rt_toml)

    viper_add_test(test_rt_markdown ${VIPER_TESTS_DIR}/runtime/RTMarkdownTests.cpp)
    target_link_libraries(test_rt_markdown PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_markdown test_rt_markdown)

    viper_add_test(test_rt_cancellation ${VIPER_TESTS_DIR}/runtime/RTCancellationTests.cpp)
    target_link_libraries(test_rt_cancellation PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_cancellation test_rt_cancellation)

    viper_add_test(test_rt_jsonpath ${VIPER_TESTS_DIR}/runtime/RTJsonPathTests.cpp)
    target_link_libraries(test_rt_jsonpath PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_jsonpath test_rt_jsonpath)

    viper_add_test(test_rt_debounce ${VIPER_TESTS_DIR}/runtime/RTDebounceTests.cpp)
    target_link_libraries(test_rt_debounce PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_debounce test_rt_debounce)

    viper_add_test(test_rt_retry ${VIPER_TESTS_DIR}/runtime/RTRetryTests.cpp)
    target_link_libraries(test_rt_retry PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_retry test_rt_retry)

    viper_add_test(test_rt_binbuf ${VIPER_TESTS_DIR}/runtime/RTBinaryBufferTests.cpp)
    target_link_libraries(test_rt_binbuf PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_binbuf test_rt_binbuf)

    viper_add_test(test_rt_weakmap ${VIPER_TESTS_DIR}/runtime/RTWeakMapTests.cpp)
    target_link_libraries(test_rt_weakmap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_weakmap test_rt_weakmap)

    viper_add_test(test_rt_scheduler ${VIPER_TESTS_DIR}/runtime/RTSchedulerTests.cpp)
    target_link_libraries(test_rt_scheduler PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_scheduler test_rt_scheduler)

    viper_add_test(test_rt_ratelimit ${VIPER_TESTS_DIR}/runtime/RTRateLimitTests.cpp)
    target_link_libraries(test_rt_ratelimit PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_ratelimit test_rt_ratelimit)

    viper_add_test(test_rt_html ${VIPER_TESTS_DIR}/runtime/RTHtmlTests.cpp)
    target_link_libraries(test_rt_html PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_html test_rt_html)

    viper_add_test(test_rt_iter ${VIPER_TESTS_DIR}/runtime/RTIterTests.cpp)
    target_link_libraries(test_rt_iter PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_iter test_rt_iter)

    viper_add_test(test_rt_spritesheet ${VIPER_TESTS_DIR}/runtime/RTSpriteSheetTests.cpp)
    target_link_libraries(test_rt_spritesheet PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_spritesheet test_rt_spritesheet)

    viper_add_test(test_rt_physics2d ${VIPER_TESTS_DIR}/runtime/RTPhysics2DTests.cpp)
    target_link_libraries(test_rt_physics2d PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_physics2d test_rt_physics2d)

    viper_add_test(test_rt_audio_integration ${VIPER_TESTS_DIR}/runtime/RTAudioIntegrationTests.cpp)
    target_link_libraries(test_rt_audio_integration PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_audio_integration test_rt_audio_integration)

    viper_add_test(test_rt_heap_alloc ${VIPER_TESTS_DIR}/runtime/RTHeapAllocTests.cpp)
    target_link_libraries(test_rt_heap_alloc PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_heap_alloc test_rt_heap_alloc)

    viper_add_test(test_rt_object_introspect ${VIPER_TESTS_DIR}/runtime/RTObjectIntrospectTests.cpp)
    target_link_libraries(test_rt_object_introspect PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_object_introspect test_rt_object_introspect)

    viper_add_test(test_rt_arithmetic_ub ${VIPER_TESTS_DIR}/runtime/RTArithmeticUBTests.c)
    target_link_libraries(test_rt_arithmetic_ub PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_arithmetic_ub test_rt_arithmetic_ub)

    viper_add_test(test_rt_buffer_overflow ${VIPER_TESTS_DIR}/runtime/RTBufferOverflowTests.c)
    target_link_libraries(test_rt_buffer_overflow PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_buffer_overflow test_rt_buffer_overflow)

    viper_add_test(test_rt_concurrency_bugs ${VIPER_TESTS_DIR}/runtime/RTConcurrencyTests.c)
    target_link_libraries(test_rt_concurrency_bugs PRIVATE ${VIPER_RUNTIME_TEST_LIBS} Threads::Threads)
    viper_add_ctest(test_rt_concurrency_bugs test_rt_concurrency_bugs)

    viper_add_test(test_rt_graphics_mem ${VIPER_TESTS_DIR}/runtime/RTGraphicsMemTests.c)
    target_link_libraries(test_rt_graphics_mem PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_graphics_mem test_rt_graphics_mem)

    viper_add_test(test_rt_null_checks ${VIPER_TESTS_DIR}/runtime/RTNullCheckTests.c)
    target_link_libraries(test_rt_null_checks PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_null_checks test_rt_null_checks)

    viper_add_test(test_rt_realloc_mem ${VIPER_TESTS_DIR}/runtime/RTReallocMemTests.c)
    target_link_libraries(test_rt_realloc_mem PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_realloc_mem test_rt_realloc_mem)

    viper_add_test(test_rt_security_fixes ${VIPER_TESTS_DIR}/runtime/RTSecurityFixesTests.c)
    target_link_libraries(test_rt_security_fixes PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_security_fixes test_rt_security_fixes)

    viper_add_test(test_rt_optimization_fixes ${VIPER_TESTS_DIR}/runtime/RTOptimizationFixesTests.c)
    target_link_libraries(test_rt_optimization_fixes PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_optimization_fixes test_rt_optimization_fixes)

    # P2 optimisation tests
    viper_add_test(test_rt_vec2_pool ${VIPER_TESTS_DIR}/runtime/RTVec2PoolTests.cpp)
    target_link_libraries(test_rt_vec2_pool PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_vec2_pool test_rt_vec2_pool)

    viper_add_test(test_rt_vec3_pool ${VIPER_TESTS_DIR}/runtime/RTVec3PoolTests.cpp)
    target_link_libraries(test_rt_vec3_pool PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_vec3_pool test_rt_vec3_pool)

    viper_add_test(test_rt_list_i64 ${VIPER_TESTS_DIR}/runtime/RTListI64Tests.cpp)
    target_link_libraries(test_rt_list_i64 PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_list_i64 test_rt_list_i64)

    viper_add_test(test_rt_string_intern ${VIPER_TESTS_DIR}/runtime/RTStringInternTests.cpp)
    target_link_libraries(test_rt_string_intern PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_intern test_rt_string_intern)

    viper_add_test(test_rt_thread_safety ${VIPER_TESTS_DIR}/runtime/RTThreadSafetyTests.cpp)
    target_link_libraries(test_rt_thread_safety PRIVATE ${VIPER_RUNTIME_TEST_LIBS} il_runtime)
    viper_add_ctest(test_rt_thread_safety test_rt_thread_safety)

    # Resource leak detection tests
    viper_add_test(test_rt_resource_leak ${VIPER_TESTS_DIR}/runtime/RTResourceLeakTests.cpp)
    target_link_libraries(test_rt_resource_leak PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_resource_leak test_rt_resource_leak)

    # BASIC parser recursion depth limit tests
    if (NOT TARGET test_basic_depth_limits)
        viper_add_test(test_basic_depth_limits ${VIPER_TESTS_DIR}/unit/test_basic_depth_limits.cpp)
        target_link_libraries(test_basic_depth_limits PRIVATE fe_basic)
        viper_add_ctest(test_basic_depth_limits test_basic_depth_limits)
    endif ()

    viper_add_test(test_source_loader_limits ${VIPER_TESTS_DIR}/unit/test_source_loader_limits.cpp)
    target_link_libraries(test_source_loader_limits PRIVATE il_tools_common viper_test_common)
    viper_add_ctest(test_source_loader_limits test_source_loader_limits)
endfunction()
