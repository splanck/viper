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

# Poll callback tests
if (NOT TARGET test_vm_poll_callback)
    viper_add_test(test_vm_poll_callback ${VIPER_TESTS_DIR}/unit/VM_PollCallbackTests.cpp)
    target_link_libraries(test_vm_poll_callback PRIVATE il_build il_vm viper_support)
    viper_add_ctest(test_vm_poll_callback test_vm_poll_callback)
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

function(viper_add_runtime_tests)
    if (TARGET test_rt_string)
        return()
    endif ()

    viper_add_test(test_rt_string ${VIPER_TESTS_DIR}/unit/test_rt_string.cpp)
    target_link_libraries(test_rt_string PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_string test_rt_string)

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
    target_link_libraries(test_rt_term_color PRIVATE ${VIPER_RUNTIME_TEST_LIBS} util)
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

    # Sleep runtime test
    viper_add_test(test_rt_sleep ${VIPER_TESTS_DIR}/runtime/RT_SleepTests.cpp)
    target_link_libraries(test_rt_sleep PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_sleep test_rt_sleep)

    # Timer runtime test
    viper_add_test(test_rt_timer ${VIPER_TESTS_DIR}/runtime/RT_TimerTests.cpp)
    target_link_libraries(test_rt_timer PRIVATE ${VIPER_RUNTIME_TEST_LIBS})
    viper_add_ctest(test_rt_timer test_rt_timer)

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
endfunction()
