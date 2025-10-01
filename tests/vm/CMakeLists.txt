# SPDX-License-Identifier: MIT
# File: tests/vm/CMakeLists.txt
# Purpose: Tests covering the VM runtime and debugger support.

set(VIPER_VM_LIB il_vm)
set(VIPER_VM_LIB ${VIPER_VM_LIB} PARENT_SCOPE)
set(VIPER_VM_SUPPORT_LIB support)
set(VIPER_VM_SUPPORT_LIB ${VIPER_VM_SUPPORT_LIB} PARENT_SCOPE)

function(viper_add_vm_debug_tests)
  if(TARGET test_vm_trace_il)
    return()
  endif()

  set(_VIPER_VM_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

  viper_add_test_exe(test_vm_trace_il ${_VIPER_VM_DIR}/TraceILTests.cpp)
  viper_add_ctest(test_vm_trace_il
    test_vm_trace_il
    $<TARGET_FILE:ilc>
    ${CMAKE_SOURCE_DIR}/examples/il/trace_min.il
    ${CMAKE_SOURCE_DIR}/tests/vm/trace_min.trace)

  viper_add_test_exe(test_vm_break_label ${_VIPER_VM_DIR}/BreakLabelTests.cpp)
  viper_add_ctest(test_vm_break_label
    test_vm_break_label
    $<TARGET_FILE:ilc>
    ${CMAKE_SOURCE_DIR}/examples/il/break_label.il)

  viper_add_test_exe(test_vm_debug_script ${_VIPER_VM_DIR}/DebugScriptTests.cpp)
  viper_add_ctest(test_vm_debug_script
    test_vm_debug_script
    $<TARGET_FILE:ilc>
    ${CMAKE_SOURCE_DIR}/examples/il/debug_script.il
    ${CMAKE_SOURCE_DIR}/examples/il/debug_script.txt)

  viper_add_test_exe(test_vm_watch ${_VIPER_VM_DIR}/WatchTests.cpp)
  viper_add_ctest(test_vm_watch
    test_vm_watch
    $<TARGET_FILE:ilc>
    ${CMAKE_SOURCE_DIR}/tests/vm/WatchTests.il)

  viper_add_test_exe(test_vm_block_param_step ${_VIPER_VM_DIR}/BlockParamStepTests.cpp)
  viper_add_ctest(test_vm_block_param_step
    test_vm_block_param_step
    $<TARGET_FILE:ilc>
    ${CMAKE_SOURCE_DIR}/tests/vm/BlockParamStep.il
    ${CMAKE_SOURCE_DIR}/tests/vm/BlockParamStep.txt)

  viper_add_test_exe(test_vm_summary ${_VIPER_VM_DIR}/SummaryTests.cpp)
  viper_add_ctest(test_vm_summary
    test_vm_summary
    $<TARGET_FILE:ilc>
    ${CMAKE_SOURCE_DIR}/examples/il/summary.il)
endfunction()

function(viper_add_vm_unit_tests)
  if(TARGET test_vm_trap_loc)
    return()
  endif()

  set(_VIPER_VM_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

  viper_add_test_exe(test_vm_trap_domain_error ${_VIPER_VM_DIR}/TrapDomainErrorTests.cpp)
  target_link_libraries(test_vm_trap_domain_error PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_trap_domain_error test_vm_trap_domain_error)

  viper_add_test_exe(test_vm_trap_divide_by_zero ${_VIPER_VM_DIR}/TrapDivideByZeroTests.cpp)
  target_link_libraries(test_vm_trap_divide_by_zero PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_trap_divide_by_zero test_vm_trap_divide_by_zero)

  viper_add_test_exe(test_vm_trap_overflow ${_VIPER_VM_DIR}/TrapOverflowTests.cpp)
  target_link_libraries(test_vm_trap_overflow PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_trap_overflow test_vm_trap_overflow)

  viper_add_test_exe(test_vm_errors_core ${_VIPER_VM_DIR}/ErrorsCoreTests.cpp)
  target_link_libraries(test_vm_errors_core PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_errors_core test_vm_errors_core)

  viper_add_test_exe(test_vm_errors_eh ${_VIPER_VM_DIR}/ErrorsEhTests.cpp)
  target_link_libraries(test_vm_errors_eh PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_errors_eh test_vm_errors_eh)

  viper_add_test_exe(test_vm_idx_chk ${_VIPER_VM_DIR}/IdxChkTests.cpp)
  target_link_libraries(test_vm_idx_chk PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_idx_chk test_vm_idx_chk)

  viper_add_test_exe(test_vm_int_ops ${_VIPER_VM_DIR}/IntOpsTests.cpp)
  target_link_libraries(test_vm_int_ops PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_int_ops test_vm_int_ops)

  viper_add_test_exe(test_vm_pow ${_VIPER_VM_DIR}/PowTests.cpp)
  target_link_libraries(test_vm_pow PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_pow test_vm_pow)

  viper_add_test_exe(test_vm_trap_invalid_cast ${_VIPER_VM_DIR}/TrapInvalidCastTests.cpp)
  target_link_libraries(test_vm_trap_invalid_cast PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_trap_invalid_cast test_vm_trap_invalid_cast)

  viper_add_test_exe(test_vm_trap_loc ${VIPER_TESTS_DIR}/unit/test_vm_trap_loc.cpp)
  target_link_libraries(test_vm_trap_loc PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_trap_loc test_vm_trap_loc)

  viper_add_test_exe(test_vm_rt_trap_loc ${VIPER_TESTS_DIR}/unit/test_vm_rt_trap_loc.cpp)
  target_link_libraries(test_vm_rt_trap_loc PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_rt_trap_loc test_vm_rt_trap_loc)

  viper_add_test_exe(test_vm_rt_missing_arg ${VIPER_TESTS_DIR}/unit/test_vm_rt_missing_arg.cpp)
  target_link_libraries(test_vm_rt_missing_arg PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_rt_missing_arg test_vm_rt_missing_arg)

  viper_add_test_exe(test_vm_rt_extra_arg ${VIPER_TESTS_DIR}/unit/test_vm_rt_extra_arg.cpp)
  target_link_libraries(test_vm_rt_extra_arg PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_rt_extra_arg test_vm_rt_extra_arg)

  viper_add_test_exe(test_vm_rt_concat_missing_args ${VIPER_TESTS_DIR}/unit/test_vm_rt_concat_missing_args.cpp)
  target_link_libraries(test_vm_rt_concat_missing_args PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_rt_concat_missing_args test_vm_rt_concat_missing_args)

  viper_add_test_exe(test_vm_rt_unknown_helper ${VIPER_TESTS_DIR}/unit/test_vm_rt_unknown_helper.cpp)
  target_link_libraries(test_vm_rt_unknown_helper PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_rt_unknown_helper test_vm_rt_unknown_helper)

  viper_add_test_exe(test_vm_runtime_concurrency ${VIPER_TESTS_DIR}/unit/test_vm_runtime_concurrency.cpp)
  target_link_libraries(test_vm_runtime_concurrency PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB} Threads::Threads)
  viper_add_ctest(test_vm_runtime_concurrency test_vm_runtime_concurrency)

  viper_add_test_exe(test_vm_runtime_bridge_marshalling ${VIPER_TESTS_DIR}/unit/test_vm_runtime_bridge_marshalling.cpp)
  target_link_libraries(test_vm_runtime_bridge_marshalling PRIVATE ${VIPER_VM_LIB})
  viper_add_ctest(test_vm_runtime_bridge_marshalling test_vm_runtime_bridge_marshalling)

  viper_add_test_exe(test_vm_slot_access ${VIPER_TESTS_DIR}/unit/test_vm_slot_access.cpp)
  target_link_libraries(test_vm_slot_access PRIVATE ${VIPER_VM_LIB})
  viper_add_ctest(test_vm_slot_access test_vm_slot_access)

  viper_add_test_exe(test_vm_alloca_negative ${VIPER_TESTS_DIR}/unit/test_vm_alloca_negative.cpp)
  target_link_libraries(test_vm_alloca_negative PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_alloca_negative test_vm_alloca_negative)

  viper_add_test_exe(test_vm_alloca_missing_size ${VIPER_TESTS_DIR}/unit/test_vm_alloca_missing_size.cpp)
  target_link_libraries(test_vm_alloca_missing_size PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_alloca_missing_size test_vm_alloca_missing_size)

  viper_add_test_exe(test_vm_alloca_overflow ${VIPER_TESTS_DIR}/unit/test_vm_alloca_overflow.cpp)
  target_link_libraries(test_vm_alloca_overflow PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_alloca_overflow test_vm_alloca_overflow)

  viper_add_test_exe(test_vm_unknown_global ${VIPER_TESTS_DIR}/unit/test_vm_unknown_global.cpp)
  target_link_libraries(test_vm_unknown_global PRIVATE il_build ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_unknown_global test_vm_unknown_global)

  viper_add_test_exe(test_vm_many_temps ${VIPER_TESTS_DIR}/unit/test_vm_many_temps.cpp)
  target_link_libraries(test_vm_many_temps PRIVATE ${VIPER_VM_LIB} ${VIPER_VM_SUPPORT_LIB})
  viper_add_ctest(test_vm_many_temps test_vm_many_temps)

  viper_add_test_exe(test_vm_addr_of ${VIPER_TESTS_DIR}/unit/test_vm_addr_of.cpp)
  target_link_libraries(test_vm_addr_of PRIVATE il_io ${VIPER_VM_LIB} il_api)
  viper_add_ctest(test_vm_addr_of test_vm_addr_of)

  viper_add_test_exe(test_vm_opcode_dispatch ${VIPER_TESTS_DIR}/unit/test_vm_opcode_dispatch.cpp)
  target_link_libraries(test_vm_opcode_dispatch PRIVATE il_io ${VIPER_VM_LIB} il_api)
  viper_add_ctest(test_vm_opcode_dispatch test_vm_opcode_dispatch)
endfunction()

function(viper_add_vm_path_tests)
  if(TARGET test_vm_normalize_path)
    return()
  endif()

  viper_add_test_exe(test_vm_normalize_path ${VIPER_TESTS_DIR}/unit/test_vm_normalize_path.cpp)
  target_link_libraries(test_vm_normalize_path PRIVATE ${VIPER_VM_LIB})
  viper_add_ctest(test_vm_normalize_path test_vm_normalize_path)

  viper_add_test_exe(test_path_normalize ${VIPER_TESTS_DIR}/unit/PathNormalizeTests.cpp)
  target_link_libraries(test_path_normalize PRIVATE ${VIPER_VM_LIB})
  viper_add_ctest(test_path_normalize test_path_normalize)
endfunction()
