# SPDX-License-Identifier: GPL-3.0-only
# File: tests/vm/CMakeLists.txt
# Purpose: Tests covering the VM runtime and debugger support.

set(ZANNA_VM_LIB il_vm)
set(ZANNA_VM_LIB ${ZANNA_VM_LIB} PARENT_SCOPE)
set(ZANNA_VM_SUPPORT_LIB zanna_support)
set(ZANNA_VM_SUPPORT_LIB ${ZANNA_VM_SUPPORT_LIB} PARENT_SCOPE)

function(zanna_add_vm_debug_tests)
    if (TARGET test_vm_trace_il)
        return()
    endif ()

    set(_ZANNA_VM_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

    zanna_add_test(test_vm_trace_il ${_ZANNA_VM_DIR}/TraceILTests.cpp NO_CTEST)
    zanna_add_ctest(test_vm_trace_il
            test_vm_trace_il
            $<TARGET_FILE:zanna>
            ${CMAKE_SOURCE_DIR}/examples/il/trace_min.il
            ${CMAKE_SOURCE_DIR}/src/tests/vm/trace_min.trace)

    zanna_add_test(test_vm_break_label ${_ZANNA_VM_DIR}/BreakLabelTests.cpp NO_CTEST)
    zanna_add_ctest(test_vm_break_label
            test_vm_break_label
            $<TARGET_FILE:zanna>
            ${CMAKE_SOURCE_DIR}/examples/il/break_label.il)

    zanna_add_test(test_vm_debug_script ${_ZANNA_VM_DIR}/DebugScriptTests.cpp NO_CTEST)
    zanna_add_ctest(test_vm_debug_script
            test_vm_debug_script
            $<TARGET_FILE:zanna>
            ${CMAKE_SOURCE_DIR}/examples/il/debug_script.il
            ${CMAKE_SOURCE_DIR}/examples/il/debug_script.txt)

    zanna_add_test(test_vm_watch ${_ZANNA_VM_DIR}/WatchTests.cpp NO_CTEST)
    zanna_add_ctest(test_vm_watch
            test_vm_watch
            $<TARGET_FILE:zanna>
            ${CMAKE_SOURCE_DIR}/src/tests/vm/WatchTests.il)

    zanna_add_test(test_vm_block_param_step ${_ZANNA_VM_DIR}/BlockParamStepTests.cpp NO_CTEST)
    zanna_add_ctest(test_vm_block_param_step
            test_vm_block_param_step
            $<TARGET_FILE:zanna>
            ${CMAKE_SOURCE_DIR}/src/tests/vm/BlockParamStep.il
            ${CMAKE_SOURCE_DIR}/src/tests/vm/BlockParamStep.txt)

    zanna_add_test(test_vm_summary ${_ZANNA_VM_DIR}/SummaryTests.cpp NO_CTEST)
    zanna_add_ctest(test_vm_summary
            test_vm_summary
            $<TARGET_FILE:zanna>
            ${CMAKE_SOURCE_DIR}/examples/il/summary.il)
endfunction()

function(zanna_add_vm_unit_tests)
    if (TARGET test_vm_trap_loc)
        return()
    endif ()

    set(_ZANNA_VM_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

    zanna_add_test(test_vm_trap_domain_error ${_ZANNA_VM_DIR}/TrapDomainErrorTests.cpp)
    target_link_libraries(test_vm_trap_domain_error PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_trap_domain_error test_vm_trap_domain_error)

    zanna_add_test(test_vm_trap_divide_by_zero ${_ZANNA_VM_DIR}/TrapDivideByZeroTests.cpp)
    target_link_libraries(test_vm_trap_divide_by_zero PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_trap_divide_by_zero test_vm_trap_divide_by_zero)

    zanna_add_test(test_vm_trap_overflow ${_ZANNA_VM_DIR}/TrapOverflowTests.cpp)
    target_link_libraries(test_vm_trap_overflow PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_trap_overflow test_vm_trap_overflow)

    zanna_add_test(test_vm_trap_kind ${ZANNA_TESTS_DIR}/unit/test_vm_trap_kind.cpp)
    target_link_libraries(test_vm_trap_kind PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_trap_kind test_vm_trap_kind)

    zanna_add_test(test_vm_errors_core ${_ZANNA_VM_DIR}/ErrorsCoreTests.cpp)
    target_link_libraries(test_vm_errors_core PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_errors_core test_vm_errors_core)

    zanna_add_test(test_vm_errors_eh ${_ZANNA_VM_DIR}/ErrorsEhTests.cpp)
    target_link_libraries(test_vm_errors_eh PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_errors_eh test_vm_errors_eh)

    zanna_add_test(test_vm_idx_chk ${_ZANNA_VM_DIR}/IdxChkTests.cpp)
    target_link_libraries(test_vm_idx_chk PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_idx_chk test_vm_idx_chk)

    zanna_add_test(test_vm_block_param_str_release ${_ZANNA_VM_DIR}/BlockParamStringReleaseTests.cpp)
    target_link_libraries(test_vm_block_param_str_release PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_block_param_str_release test_vm_block_param_str_release)

    zanna_add_test(test_vm_branch_param_str_retain ${_ZANNA_VM_DIR}/BranchParamStringRetainTests.cpp)
    target_link_libraries(test_vm_branch_param_str_retain PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_branch_param_str_retain test_vm_branch_param_str_retain)

    zanna_add_test(test_vm_entry_param_str_retain ${_ZANNA_VM_DIR}/EntryParamStringRetainTests.cpp)
    target_link_libraries(test_vm_entry_param_str_retain PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_entry_param_str_retain test_vm_entry_param_str_retain)

    zanna_add_test(test_vm_store_str_release ${_ZANNA_VM_DIR}/StoreStringReleaseTests.cpp)
    target_link_libraries(test_vm_store_str_release PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_store_str_release test_vm_store_str_release)

    zanna_add_test(test_vm_cast_ops ${_ZANNA_VM_DIR}/CastOpsTests.cpp)
    target_link_libraries(test_vm_cast_ops PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_cast_ops test_vm_cast_ops)

    zanna_add_test(test_vm_unsigned_narrow_cast ${_ZANNA_VM_DIR}/UnsignedNarrowCastTests.cpp)
    target_link_libraries(test_vm_unsigned_narrow_cast PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_unsigned_narrow_cast test_vm_unsigned_narrow_cast)

    zanna_add_test(test_vm_const_null ${_ZANNA_VM_DIR}/ConstNullTests.cpp)
    target_link_libraries(test_vm_const_null PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_const_null test_vm_const_null)

    zanna_add_test(test_vm_int_ops ${_ZANNA_VM_DIR}/IntOpsTests.cpp)
    target_link_libraries(test_vm_int_ops PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_int_ops test_vm_int_ops)

    zanna_add_test(test_vm_inline_literal_cache ${_ZANNA_VM_DIR}/InlineLiteralCacheTests.cpp)
    target_link_libraries(test_vm_inline_literal_cache PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_inline_literal_cache test_vm_inline_literal_cache)

    zanna_add_test(test_vm_int_unsigned_ops ${_ZANNA_VM_DIR}/IntUnsignedOpsTests.cpp)
    target_link_libraries(test_vm_int_unsigned_ops PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_int_unsigned_ops test_vm_int_unsigned_ops)

    zanna_add_test(test_vm_pow ${_ZANNA_VM_DIR}/PowTests.cpp)
    target_link_libraries(test_vm_pow PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_pow test_vm_pow)

    zanna_add_test(test_vm_dispatch_host_call ${_ZANNA_VM_DIR}/DispatchHostCallTests.cpp)
    target_link_libraries(test_vm_dispatch_host_call PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_dispatch_host_call test_vm_dispatch_host_call)

    zanna_add_test(
            test_vm_switch
            ${_ZANNA_VM_DIR}/SwitchTests.cpp
            TEST_NAME vm_SwitchTests)
    target_link_libraries(test_vm_switch PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    target_compile_definitions(test_vm_switch PRIVATE TESTS_DIR="${CMAKE_SOURCE_DIR}/src/tests")

    zanna_add_test(
            test_vm_switch_dispatch
            ${_ZANNA_VM_DIR}/SwitchDispatchTests.cpp
            TEST_NAME vm_SwitchDispatchTests)
    target_link_libraries(test_vm_switch_dispatch PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)

    zanna_add_test(test_vm_trap_invalid_cast ${_ZANNA_VM_DIR}/TrapInvalidCastTests.cpp)
    target_link_libraries(test_vm_trap_invalid_cast PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_trap_invalid_cast test_vm_trap_invalid_cast)

    zanna_add_test(test_vm_fetch_opcode_trap ${_ZANNA_VM_DIR}/FetchOpcodeTrapTests.cpp)
    target_link_libraries(test_vm_fetch_opcode_trap PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_fetch_opcode_trap test_vm_fetch_opcode_trap)

    zanna_add_test(
            test_vm_unknown_opcode
            ${_ZANNA_VM_DIR}/UnknownOpcodeTests.cpp
            TEST_NAME vm_UnknownOpcodeTests)
    target_link_libraries(test_vm_unknown_opcode PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)

    zanna_add_test(test_vm_branch_arg_mismatch ${_ZANNA_VM_DIR}/BranchArgMismatchTests.cpp)
    target_link_libraries(test_vm_branch_arg_mismatch PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_branch_arg_mismatch test_vm_branch_arg_mismatch)

    zanna_add_test(test_vm_trap_loc ${ZANNA_TESTS_DIR}/unit/test_vm_trap_loc.cpp)
    target_link_libraries(test_vm_trap_loc PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_trap_loc test_vm_trap_loc)

    zanna_add_test(test_vm_rt_trap_loc ${ZANNA_TESTS_DIR}/unit/test_vm_rt_trap_loc.cpp)
    target_link_libraries(test_vm_rt_trap_loc PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_rt_trap_loc test_vm_rt_trap_loc)

    zanna_add_test(test_vm_rt_missing_arg ${ZANNA_TESTS_DIR}/unit/test_vm_rt_missing_arg.cpp)
    target_link_libraries(test_vm_rt_missing_arg PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_rt_missing_arg test_vm_rt_missing_arg)

    zanna_add_test(test_vm_rt_extra_arg ${ZANNA_TESTS_DIR}/unit/test_vm_rt_extra_arg.cpp)
    target_link_libraries(test_vm_rt_extra_arg PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_rt_extra_arg test_vm_rt_extra_arg)

    zanna_add_test(test_vm_entry_arg_mismatch ${ZANNA_TESTS_DIR}/unit/test_vm_entry_arg_mismatch.cpp)
    target_link_libraries(test_vm_entry_arg_mismatch PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_entry_arg_mismatch test_vm_entry_arg_mismatch)

    zanna_add_test(test_vm_rt_concat_missing_args ${ZANNA_TESTS_DIR}/unit/test_vm_rt_concat_missing_args.cpp)
    target_link_libraries(test_vm_rt_concat_missing_args PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_rt_concat_missing_args test_vm_rt_concat_missing_args)

    zanna_add_test(test_vm_rt_unknown_helper ${ZANNA_TESTS_DIR}/unit/test_vm_rt_unknown_helper.cpp)
    target_link_libraries(test_vm_rt_unknown_helper PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_rt_unknown_helper test_vm_rt_unknown_helper)

    # Multi-VM isolation tests (per-VM RtContext state)
    zanna_add_test(test_vm_multi_isolation ${_ZANNA_VM_DIR}/MultiVMIsolationTests.cpp)
    target_link_libraries(test_vm_multi_isolation PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_multi_isolation test_vm_multi_isolation)

    # Per-VM extern registry isolation tests
    zanna_add_test(test_vm_per_vm_extern_registry ${_ZANNA_VM_DIR}/PerVMExternRegistryTests.cpp)
    target_link_libraries(test_vm_per_vm_extern_registry PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common Threads::Threads)
    zanna_add_ctest(test_vm_per_vm_extern_registry test_vm_per_vm_extern_registry)

    zanna_add_test(test_vm_runtime_concurrency ${ZANNA_TESTS_DIR}/unit/test_vm_runtime_concurrency.cpp)
    target_link_libraries(test_vm_runtime_concurrency PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} Threads::Threads)
    zanna_add_ctest(test_vm_runtime_concurrency test_vm_runtime_concurrency)

    zanna_add_test(test_vm_threading_model ${ZANNA_TESTS_DIR}/unit/test_vm_threading_model.cpp)
    target_link_libraries(test_vm_threading_model PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} Threads::Threads)
    zanna_add_ctest(test_vm_threading_model test_vm_threading_model)

    # Zanna.Threads integration tests (Thread.Start VM override + shared globals)
    zanna_add_test(test_vm_threads_runtime ${_ZANNA_VM_DIR}/ThreadsRuntimeTests.cpp)
    target_link_libraries(test_vm_threads_runtime PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} Threads::Threads)
    zanna_add_ctest(test_vm_threads_runtime test_vm_threads_runtime)

    zanna_add_test(test_vm_http_server_runtime ${_ZANNA_VM_DIR}/HttpServerRuntimeTests.cpp)
    target_link_libraries(test_vm_http_server_runtime PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} Threads::Threads)
    zanna_add_ctest(test_vm_http_server_runtime test_vm_http_server_runtime)

    # Concurrency stress test (many VMs across threads with traps and callbacks)
    zanna_add_test(test_vm_concurrency_stress ${ZANNA_TESTS_DIR}/unit/test_vm_concurrency_stress.cpp)
    target_link_libraries(test_vm_concurrency_stress PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} Threads::Threads)
    zanna_add_ctest(test_vm_concurrency_stress test_vm_concurrency_stress)

    # Flaky test - excluded from CTest, run manually: ./build/src/tests/vm/test_vm_runtime_trap_metadata
    zanna_add_test(test_vm_runtime_trap_metadata ${ZANNA_TESTS_DIR}/unit/test_vm_runtime_trap_metadata.cpp NO_CTEST)
    target_link_libraries(test_vm_runtime_trap_metadata PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)

    zanna_add_test(test_vm_switch_block_label ${ZANNA_TESTS_DIR}/unit/test_vm_switch_block_label.cpp)
    target_link_libraries(test_vm_switch_block_label PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_switch_block_label test_vm_switch_block_label)

    zanna_add_test(test_vm_runtime_bridge_marshalling ${ZANNA_TESTS_DIR}/unit/test_vm_runtime_bridge_marshalling.cpp)
    target_link_libraries(test_vm_runtime_bridge_marshalling PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_runtime_bridge_marshalling test_vm_runtime_bridge_marshalling)

    zanna_add_test(test_vm_marshal_constants ${ZANNA_TESTS_DIR}/unit/test_vm_marshal_constants.cpp)
    target_link_libraries(test_vm_marshal_constants PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_marshal_constants test_vm_marshal_constants)

    zanna_add_test(test_vm_runtime_stack_sync ${ZANNA_TESTS_DIR}/unit/test_vm_runtime_stack_sync.cpp)
    target_link_libraries(test_vm_runtime_stack_sync PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_runtime_stack_sync test_vm_runtime_stack_sync)

    zanna_add_test(test_vm_alloca_negative ${ZANNA_TESTS_DIR}/unit/test_vm_alloca_negative.cpp)
    target_link_libraries(test_vm_alloca_negative PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_alloca_negative test_vm_alloca_negative)

    zanna_add_test(test_vm_alloca_missing_size ${ZANNA_TESTS_DIR}/unit/test_vm_alloca_missing_size.cpp)
    target_link_libraries(test_vm_alloca_missing_size PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_alloca_missing_size test_vm_alloca_missing_size)

    zanna_add_test(test_vm_alloca_overflow ${ZANNA_TESTS_DIR}/unit/test_vm_alloca_overflow.cpp)
    target_link_libraries(test_vm_alloca_overflow PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_alloca_overflow test_vm_alloca_overflow)

    zanna_add_test(test_vm_stack_size_config ${ZANNA_TESTS_DIR}/unit/test_vm_stack_size_config.cpp)
    target_link_libraries(test_vm_stack_size_config PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_stack_size_config test_vm_stack_size_config)

    zanna_add_test(test_vm_unknown_global ${ZANNA_TESTS_DIR}/unit/test_vm_unknown_global.cpp)
    target_link_libraries(test_vm_unknown_global PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_unknown_global test_vm_unknown_global)

    zanna_add_test(test_vm_error_resume_load_store ${ZANNA_TESTS_DIR}/unit/test_vm_error_resume_load_store.cpp)
    target_link_libraries(test_vm_error_resume_load_store PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_error_resume_load_store test_vm_error_resume_load_store)

    zanna_add_test(test_vm_null_mem_ops ${ZANNA_TESTS_DIR}/unit/test_vm_null_mem_ops.cpp)
    target_link_libraries(test_vm_null_mem_ops PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_null_mem_ops test_vm_null_mem_ops)

    zanna_add_test(test_vm_gep_nullptr ${ZANNA_TESTS_DIR}/unit/test_vm_gep_nullptr.cpp)
    target_link_libraries(test_vm_gep_nullptr PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_gep_nullptr test_vm_gep_nullptr)

    zanna_add_test(test_vm_gep_offsets ${ZANNA_TESTS_DIR}/unit/test_vm_gep_offsets.cpp)
    target_link_libraries(test_vm_gep_offsets PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_gep_offsets test_vm_gep_offsets)

    # VM memcpy-based stack/memory ops tests
    zanna_add_test(test_vm_stack_memcpy_ops ${ZANNA_TESTS_DIR}/unit/test_vm_stack_memcpy_ops.cpp)
    target_link_libraries(test_vm_stack_memcpy_ops PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_stack_memcpy_ops test_vm_stack_memcpy_ops)

    zanna_add_test(test_vm_many_temps ${ZANNA_TESTS_DIR}/unit/test_vm_many_temps.cpp)
    target_link_libraries(test_vm_many_temps PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_many_temps test_vm_many_temps)

    # VM buffer pooling test (exercises frame/register reuse in recursive calls)
    zanna_add_test(test_vm_buffer_pooling ${ZANNA_TESTS_DIR}/unit/test_vm_buffer_pooling.cpp)
    target_link_libraries(test_vm_buffer_pooling PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_buffer_pooling test_vm_buffer_pooling)

    zanna_add_test(test_vm_shift_mask ${ZANNA_TESTS_DIR}/unit/test_vm_shift_mask.cpp)
    target_link_libraries(test_vm_shift_mask PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_shift_mask test_vm_shift_mask)

    zanna_add_test(test_vm_addr_of ${ZANNA_TESTS_DIR}/unit/test_vm_addr_of.cpp)
    target_link_libraries(test_vm_addr_of PRIVATE zanna_il_full ${ZANNA_VM_LIB} il_api)
    zanna_add_ctest(test_vm_addr_of test_vm_addr_of)

    zanna_add_test(test_vm_dispatch_handlers ${ZANNA_TESTS_DIR}/unit/test_vm_dispatch_handlers.cpp)
    target_link_libraries(test_vm_dispatch_handlers PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_dispatch_handlers test_vm_dispatch_handlers)

    zanna_add_test(test_vm_op_schema ${ZANNA_TESTS_DIR}/unit/test_vm_op_schema.cpp)
    target_link_libraries(test_vm_op_schema PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_op_schema test_vm_op_schema)

    zanna_add_test(test_vm_opcode_coverage ${_ZANNA_VM_DIR}/OpcodeCoverageTests.cpp)
    target_link_libraries(test_vm_opcode_coverage PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_opcode_coverage test_vm_opcode_coverage)

    zanna_add_test(test_vm_dispatch_coverage ${ZANNA_TESTS_DIR}/unit/test_vm_dispatch_coverage.cpp)
    target_link_libraries(test_vm_dispatch_coverage PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_dispatch_coverage test_vm_dispatch_coverage)

    zanna_add_test(test_vm_opcode_dispatch ${ZANNA_TESTS_DIR}/unit/test_vm_opcode_dispatch.cpp)
    target_link_libraries(test_vm_opcode_dispatch PRIVATE zanna_il_full ${ZANNA_VM_LIB} il_api)
    zanna_add_ctest(test_vm_opcode_dispatch test_vm_opcode_dispatch)

    zanna_add_test(test_vm_trace_bool_consts ${ZANNA_TESTS_DIR}/unit/test_vm_trace_bool_consts.cpp)
    target_link_libraries(test_vm_trace_bool_consts PRIVATE zanna_il_full ${ZANNA_VM_LIB} il_api)
    zanna_add_ctest(test_vm_trace_bool_consts test_vm_trace_bool_consts)

    zanna_add_test(
            test_vm_trace_unicode_path
            ${ZANNA_TESTS_DIR}/unit/test_vm_trace_unicode_path.cpp)
    target_link_libraries(test_vm_trace_unicode_path PRIVATE zanna_il_full ${ZANNA_VM_LIB} il_api)
    zanna_add_ctest(test_vm_trace_unicode_path test_vm_trace_unicode_path)
    set_tests_properties(test_vm_trace_unicode_path PROPERTIES RESOURCE_LOCK vm_trace_artifacts)

    zanna_add_test(test_vm_zext1_isub ${ZANNA_TESTS_DIR}/unit/test_vm_zext1_isub.cpp)
    target_link_libraries(test_vm_zext1_isub PRIVATE zanna_il_full ${ZANNA_VM_LIB} il_api)
    zanna_add_ctest(test_vm_zext1_isub test_vm_zext1_isub)

    zanna_add_test(test_vm_run_loop_helpers ${ZANNA_TESTS_DIR}/unit/test_vm_run_loop_helpers.cpp)
    target_link_libraries(test_vm_run_loop_helpers PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_run_loop_helpers test_vm_run_loop_helpers)

    zanna_add_test(test_vm_trap_invariants ${ZANNA_TESTS_DIR}/unit/test_vm_trap_invariants.cpp)
    target_link_libraries(test_vm_trap_invariants PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_trap_invariants test_vm_trap_invariants)

    # ZannaStringHandle RAII tests
    zanna_add_test(test_zanna_string_handle ${_ZANNA_VM_DIR}/test_zanna_string_handle.cpp)
    target_link_libraries(test_zanna_string_handle PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_zanna_string_handle test_zanna_string_handle)

    # ActiveVMGuard re-entrancy tests (callbacks and nested guards)
    zanna_add_test(test_vm_reentrant_callbacks ${_ZANNA_VM_DIR}/ReentrantCallbackTests.cpp)
    target_link_libraries(test_vm_reentrant_callbacks PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_reentrant_callbacks test_vm_reentrant_callbacks)

    # Floating-point arithmetic tests (FAdd, FSub, FMul, FDiv)
    zanna_add_test(test_vm_float_arith ${_ZANNA_VM_DIR}/FloatArithTests.cpp)
    target_link_libraries(test_vm_float_arith PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_float_arith test_vm_float_arith)

    # Floating-point comparison tests (FCmpEQ, FCmpNE, FCmpLT, FCmpLE, FCmpGT, FCmpGE, FCmpOrd, FCmpUno)
    zanna_add_test(test_vm_float_compare ${_ZANNA_VM_DIR}/FloatCompareTests.cpp)
    target_link_libraries(test_vm_float_compare PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_float_compare test_vm_float_compare)

    # Signed comparison tests (SCmpLT, SCmpLE, SCmpGT, SCmpGE)
    zanna_add_test(test_vm_signed_compare ${_ZANNA_VM_DIR}/SignedCompareTests.cpp)
    target_link_libraries(test_vm_signed_compare PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_signed_compare test_vm_signed_compare)

    # Integer overflow tests (ISubOvf, IMulOvf)
    zanna_add_test(test_vm_int_overflow ${_ZANNA_VM_DIR}/IntOverflowTests.cpp)
    target_link_libraries(test_vm_int_overflow PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_int_overflow test_vm_int_overflow)

    # Float conversion tests (Sitofp, Uitofp, Fptosi, Fptoui)
    zanna_add_test(test_vm_float_convert ${_ZANNA_VM_DIR}/FloatConvertTests.cpp)
    target_link_libraries(test_vm_float_convert PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_float_convert test_vm_float_convert)

    # Bitwise XOR tests
    zanna_add_test(test_vm_bitwise_xor ${_ZANNA_VM_DIR}/BitwiseXorTests.cpp)
    target_link_libraries(test_vm_bitwise_xor PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_bitwise_xor test_vm_bitwise_xor)

    # Integer equality comparison tests (ICmpEq, ICmpNe)
    zanna_add_test(test_vm_int_equality ${_ZANNA_VM_DIR}/IntEqualityTests.cpp)
    target_link_libraries(test_vm_int_equality PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_int_equality test_vm_int_equality)

    # Cast conversion tests (CastSiToFp, CastUiToFp, CastFpToSiRteChk, CastFpToUiRteChk)
    zanna_add_test(test_vm_cast_convert ${_ZANNA_VM_DIR}/CastConvertTests.cpp)
    target_link_libraries(test_vm_cast_convert PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_cast_convert test_vm_cast_convert)

    # Narrowing cast tests (CastSiNarrowChk, CastUiNarrowChk)
    zanna_add_test(test_vm_narrow_cast ${_ZANNA_VM_DIR}/NarrowCastTests.cpp)
    target_link_libraries(test_vm_narrow_cast PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_narrow_cast test_vm_narrow_cast)

    # Trunc1/Zext1 tests (1-bit truncation and extension)
    zanna_add_test(test_vm_trunc_zext ${_ZANNA_VM_DIR}/TruncZextTests.cpp)
    target_link_libraries(test_vm_trunc_zext PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_trunc_zext test_vm_trunc_zext)

    # Basic integer arithmetic tests (Add, Sub, Mul)
    zanna_add_test(test_vm_int_basic_arith ${_ZANNA_VM_DIR}/IntBasicArithTests.cpp)
    target_link_libraries(test_vm_int_basic_arith PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_int_basic_arith test_vm_int_basic_arith)

    # Shift left tests (Shl)
    zanna_add_test(test_vm_shift_left ${_ZANNA_VM_DIR}/ShiftLeftTests.cpp)
    target_link_libraries(test_vm_shift_left PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_shift_left test_vm_shift_left)

    # Function call tests (Call)
    zanna_add_test(test_vm_call ${_ZANNA_VM_DIR}/CallTests.cpp)
    target_link_libraries(test_vm_call PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_call test_vm_call)

    # GEP pointer arithmetic tests
    zanna_add_test(test_vm_gep_arith ${_ZANNA_VM_DIR}/GepArithTests.cpp)
    target_link_libraries(test_vm_gep_arith PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_gep_arith test_vm_gep_arith)

    # AddrOf tests
    zanna_add_test(test_vm_addr_of_arith ${_ZANNA_VM_DIR}/AddrOfTests.cpp)
    target_link_libraries(test_vm_addr_of_arith PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_addr_of_arith test_vm_addr_of_arith)

    # Bounds check normalization tests (non-zero lower bound)
    zanna_add_test(test_vm_bounds_check_normalize ${_ZANNA_VM_DIR}/BoundsCheckNormalizeTests.cpp)
    target_link_libraries(test_vm_bounds_check_normalize PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_bounds_check_normalize test_vm_bounds_check_normalize)

    # TCO string retain/release ordering tests
    zanna_add_test(test_vm_tco_string_retain ${_ZANNA_VM_DIR}/TcoStringRetainTests.cpp)
    target_link_libraries(test_vm_tco_string_retain PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_tco_string_retain test_vm_tco_string_retain)

    # Class allocation stress tests (BUG-VM-001 regression)
    zanna_add_test(test_vm_class_stress ${_ZANNA_VM_DIR}/ClassStressTests.cpp)
    target_link_libraries(test_vm_class_stress PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common fe_zia)
    zanna_add_ctest(test_vm_class_stress test_vm_class_stress)

    # Priority-1 hot-path optimisation correctness tests
    # Covers: flat Slot+paramsSet (P1-3.3), VM-level SwitchCache (P1-3.4),
    #         raw fn-pointer poll trampoline (P1-3.5), FunctionExecCache (P1-3.1/3.2)
    zanna_add_test(test_vm_p1_optimizations ${_ZANNA_VM_DIR}/P1OptimizationTests.cpp)
    target_link_libraries(test_vm_p1_optimizations PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_p1_optimizations test_vm_p1_optimizations)

    # VM interrupt mechanism tests (HIGH-3: requestInterrupt/clearInterrupt API,
    # poll-triggered interrupt, normal program unaffected after clearInterrupt)
    zanna_add_test(test_vm_interrupt ${_ZANNA_VM_DIR}/InterruptTests.cpp)
    target_link_libraries(test_vm_interrupt PRIVATE il_build ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB} zanna_test_common)
    zanna_add_ctest(test_vm_interrupt test_vm_interrupt)
endfunction()

function(zanna_add_vm_path_tests)
    if (TARGET test_vm_normalize_path)
        return()
    endif ()

    zanna_add_test(test_vm_normalize_path ${ZANNA_TESTS_DIR}/unit/test_vm_normalize_path.cpp)
    target_link_libraries(test_vm_normalize_path PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_normalize_path test_vm_normalize_path)

    zanna_add_test(
            test_vm_debug_src_breakpoint ${ZANNA_TESTS_DIR}/unit/test_vm_debug_src_breakpoint.cpp)
    target_link_libraries(test_vm_debug_src_breakpoint PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_debug_src_breakpoint test_vm_debug_src_breakpoint)

    zanna_add_test(
            test_vm_debug_src_breakpoint_unknown_file
            ${ZANNA_TESTS_DIR}/unit/test_vm_debug_src_breakpoint_unknown_file.cpp)
    target_link_libraries(test_vm_debug_src_breakpoint_unknown_file
            PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_debug_src_breakpoint_unknown_file
            test_vm_debug_src_breakpoint_unknown_file)

    zanna_add_test(
            test_vm_debug_watches
            ${ZANNA_TESTS_DIR}/unit/test_vm_debug_watches.cpp)
    target_link_libraries(test_vm_debug_watches
            PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_vm_debug_watches test_vm_debug_watches)

    zanna_add_test(test_path_normalize ${ZANNA_TESTS_DIR}/unit/PathNormalizeTests.cpp)
    target_link_libraries(test_path_normalize PRIVATE ${ZANNA_VM_LIB} ${ZANNA_VM_SUPPORT_LIB})
    zanna_add_ctest(test_path_normalize test_path_normalize)
endfunction()
