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

    viper_add_test(test_rt_hash ${VIPER_TESTS_DIR}/runtime/RTHashTests.cpp)
    target_link_libraries(test_rt_hash PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_hash test_rt_hash)

    viper_add_test(test_rt_crypto ${VIPER_TESTS_DIR}/runtime/RTCryptoTests.cpp)
    target_link_libraries(test_rt_crypto PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_crypto test_rt_crypto)

    viper_add_test(test_rt_pattern ${VIPER_TESTS_DIR}/runtime/RTPatternTests.cpp)
    target_link_libraries(test_rt_pattern PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_pattern test_rt_pattern)

    viper_add_test(test_rt_template ${VIPER_TESTS_DIR}/runtime/RTTemplateTests.cpp)
    target_link_libraries(test_rt_template PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_template test_rt_template)

    viper_add_test(test_rt_compress ${VIPER_TESTS_DIR}/runtime/RTCompressTests.cpp)
    target_link_libraries(test_rt_compress PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_compress test_rt_compress)

    viper_add_test(test_rt_archive ${VIPER_TESTS_DIR}/runtime/RTArchiveTests.cpp)
    target_link_libraries(test_rt_archive PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_archive test_rt_archive)

    viper_add_test(test_rt_network ${VIPER_TESTS_DIR}/runtime/RTNetworkTests.cpp)
    target_link_libraries(test_rt_network PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_network test_rt_network)

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

    viper_add_test(test_rt_vec2 ${VIPER_TESTS_DIR}/runtime/RTVec2Tests.cpp)
    target_link_libraries(test_rt_vec2 PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_vec2 test_rt_vec2)

    viper_add_test(test_rt_vec3 ${VIPER_TESTS_DIR}/runtime/RTVec3Tests.cpp)
    target_link_libraries(test_rt_vec3 PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_vec3 test_rt_vec3)

    viper_add_test(test_rt_pixels ${VIPER_TESTS_DIR}/runtime/RTPixelsTests.cpp)
    target_link_libraries(test_rt_pixels PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_pixels test_rt_pixels)

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

    viper_add_test(test_rt_string_size_clamp ${VIPER_TESTS_DIR}/runtime/RTStringSizeClampTests.cpp)
    target_link_libraries(test_rt_string_size_clamp PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_size_clamp test_rt_string_size_clamp)

    viper_add_test(test_rt_string_ranges ${VIPER_TESTS_DIR}/runtime/RTStringRangeTests.cpp)
    target_link_libraries(test_rt_string_ranges PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_ranges test_rt_string_ranges)

    viper_add_test(test_rt_alloc ${VIPER_TESTS_DIR}/runtime/RTAllocTests.cpp)
    target_link_libraries(test_rt_alloc PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_alloc test_rt_alloc)

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

    viper_add_test(test_rt_map ${VIPER_TESTS_DIR}/runtime/RTMapTests.cpp)
    target_link_libraries(test_rt_map PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_map test_rt_map)

    viper_add_test(test_rt_treemap ${VIPER_TESTS_DIR}/runtime/RTTreeMapTests.cpp)
    target_link_libraries(test_rt_treemap PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_treemap test_rt_treemap)

    viper_add_test(test_rt_modvar ${VIPER_TESTS_DIR}/runtime/RTModvarTests.cpp)
    target_link_libraries(test_rt_modvar PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_modvar test_rt_modvar)

    viper_add_test(test_rt_bytes ${VIPER_TESTS_DIR}/runtime/RTBytesTests.cpp)
    target_link_libraries(test_rt_bytes PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_bytes test_rt_bytes)

    viper_add_test(test_rt_string_ext ${VIPER_TESTS_DIR}/runtime/RTStringExtTests.cpp)
    target_link_libraries(test_rt_string_ext PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string_ext test_rt_string_ext)
endfunction()
