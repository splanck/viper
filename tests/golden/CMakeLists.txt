# SPDX-License-Identifier: MIT
# File: tests/golden/CMakeLists.txt
# Purpose: Golden tests for BASIC diagnostics and IL optimizations.

function(viper_add_golden_suite_tests)
  if(TEST basic_error_unknown_var)
    return()
  endif()

  set(_VIPER_GOLDEN_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
  set(BASIC_ILC $<TARGET_FILE:ilc>)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/unknown_var.stderr EXPECT_UNKNOWN_VAR)
  string(STRIP "${EXPECT_UNKNOWN_VAR}" EXPECT_UNKNOWN_VAR)
  viper_add_ctest(basic_error_unknown_var
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/unknown_var.bas
    "-DEXPECT=${EXPECT_UNKNOWN_VAR}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/mismatched_next.stderr EXPECT_MISMATCHED_NEXT)
  string(STRIP "${EXPECT_MISMATCHED_NEXT}" EXPECT_MISMATCHED_NEXT)
  viper_add_ctest(basic_error_mismatched_next
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/mismatched_next.bas
    "-DEXPECT=${EXPECT_MISMATCHED_NEXT}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/loop_var_assignment.stderr EXPECT_LOOP_VAR_ASSIGNMENT)
  string(STRIP "${EXPECT_LOOP_VAR_ASSIGNMENT}" EXPECT_LOOP_VAR_ASSIGNMENT)
  viper_add_ctest(basic_error_loop_var_assignment
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/loop_var_assignment.bas
    "-DEXPECT=${EXPECT_LOOP_VAR_ASSIGNMENT}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/loop_var_input.stderr EXPECT_LOOP_VAR_INPUT)
  string(STRIP "${EXPECT_LOOP_VAR_INPUT}" EXPECT_LOOP_VAR_INPUT)
  viper_add_ctest(basic_error_loop_var_input
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/loop_var_input.bas
    "-DEXPECT=${EXPECT_LOOP_VAR_INPUT}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/bad_goto.stderr EXPECT_BAD_GOTO)
  string(STRIP "${EXPECT_BAD_GOTO}" EXPECT_BAD_GOTO)
  viper_add_ctest(basic_error_bad_goto
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/bad_goto.bas
    "-DEXPECT=${EXPECT_BAD_GOTO}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/string_arith.stderr EXPECT_STRING_ARITH)
  string(STRIP "${EXPECT_STRING_ARITH}" EXPECT_STRING_ARITH)
  viper_add_ctest(basic_error_string_arith
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/string_arith.bas
    "-DEXPECT=${EXPECT_STRING_ARITH}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/string_cmp_order.stderr EXPECT_STRING_CMP_ORDER)
  string(STRIP "${EXPECT_STRING_CMP_ORDER}" EXPECT_STRING_CMP_ORDER)
  viper_add_ctest(basic_error_string_cmp_order
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/string_cmp_order.bas
    "-DEXPECT=${EXPECT_STRING_CMP_ORDER}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/const_div_zero.stderr EXPECT_CONST_DIV_ZERO)
  string(STRIP "${EXPECT_CONST_DIV_ZERO}" EXPECT_CONST_DIV_ZERO)
  viper_add_ctest(basic_error_const_div_zero
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/const_div_zero.bas
    "-DEXPECT=${EXPECT_CONST_DIV_ZERO}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/const_divmod_zero.stderr EXPECT_CONST_DIVMOD_ZERO)
  string(STRIP "${EXPECT_CONST_DIVMOD_ZERO}" EXPECT_CONST_DIVMOD_ZERO)
  viper_add_ctest(basic_error_const_divmod_zero
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/const_divmod_zero.bas
    "-DEXPECT=${EXPECT_CONST_DIVMOD_ZERO}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/string_divmod.stderr EXPECT_STRING_DIVMOD)
  string(STRIP "${EXPECT_STRING_DIVMOD}" EXPECT_STRING_DIVMOD)
  viper_add_ctest(basic_error_string_divmod
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/string_divmod.bas
    "-DEXPECT=${EXPECT_STRING_DIVMOD}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/bad_string_intrinsics.stderr EXPECT_BAD_STRING_INTRINSICS)
  string(STRIP "${EXPECT_BAD_STRING_INTRINSICS}" EXPECT_BAD_STRING_INTRINSICS)
  viper_add_ctest(basic_error_bad_string_intrinsics
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/bad_string_intrinsics.bas
    "-DEXPECT=${EXPECT_BAD_STRING_INTRINSICS}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/float_to_int.stderr EXPECT_FLOAT_TO_INT)
  string(STRIP "${EXPECT_FLOAT_TO_INT}" EXPECT_FLOAT_TO_INT)
  viper_add_ctest(basic_error_float_to_int
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/float_to_int.bas
    "-DEXPECT=${EXPECT_FLOAT_TO_INT}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/float_in_string.stderr EXPECT_FLOAT_IN_STRING)
  string(STRIP "${EXPECT_FLOAT_IN_STRING}" EXPECT_FLOAT_IN_STRING)
  viper_add_ctest(basic_error_float_in_string
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/float_in_string.bas
    "-DEXPECT=${EXPECT_FLOAT_IN_STRING}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/bad_conversions.stderr EXPECT_BAD_CONVERSIONS)
  string(STRIP "${EXPECT_BAD_CONVERSIONS}" EXPECT_BAD_CONVERSIONS)
  viper_add_ctest(basic_error_bad_conversions
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/bad_conversions.bas
    "-DEXPECT=${EXPECT_BAD_CONVERSIONS}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/bad_math_funcs.stderr EXPECT_BAD_MATH_FUNCS)
  string(STRIP "${EXPECT_BAD_MATH_FUNCS}" EXPECT_BAD_MATH_FUNCS)
  viper_add_ctest(basic_error_bad_math_funcs
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/bad_math_funcs.bas
    "-DEXPECT=${EXPECT_BAD_MATH_FUNCS}"
    -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/vm_trap_loc.stderr EXPECT_VM_TRAP_LOC)
  string(STRIP "${EXPECT_VM_TRAP_LOC}" EXPECT_VM_TRAP_LOC)
  viper_add_ctest(vm_trap_loc
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/vm_trap_loc.il
    "-DEXPECT=${EXPECT_VM_TRAP_LOC}"
    -P ${_VIPER_GOLDEN_DIR}/check_vm_trap_loc.cmake)

  viper_add_ctest(il_opt_const_if
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/il_opt/const_if.in.il
    -DGOLDEN=${_VIPER_GOLDEN_DIR}/il_opt/const_if.opt.il
    -P ${_VIPER_GOLDEN_DIR}/il_opt/check_opt.cmake)

  viper_add_ctest(il_opt_arith_id
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/il_opt/arith_id.in.il
    -DGOLDEN=${_VIPER_GOLDEN_DIR}/il_opt/arith_id.opt.il
    -P ${_VIPER_GOLDEN_DIR}/il_opt/check_opt.cmake)

  viper_add_ctest(il_opt_math_constfold
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/il_opt/math_constfold.in.il
    -DGOLDEN=${_VIPER_GOLDEN_DIR}/il_opt/math_constfold.opt.il
    -DPASSES=constfold
    -P ${_VIPER_GOLDEN_DIR}/il_opt/check_opt.cmake)

  viper_add_ctest(il_opt_cbr_same_target
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/il_opt/cbr_same_target.in.il
    -DGOLDEN=${_VIPER_GOLDEN_DIR}/il_opt/cbr_same_target.golden
    -DPASSES=peephole
    -P ${_VIPER_GOLDEN_DIR}/il_opt/check_opt.cmake)
  set_tests_properties(il_opt_cbr_same_target PROPERTIES LABELS Smoke)

  viper_add_ctest(il_opt_mem2reg_simple
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_simple.in.il
    -DGOLDEN=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_simple.opt.il
    -DPASSES=mem2reg
    -P ${_VIPER_GOLDEN_DIR}/il_opt/check_opt.cmake)

  viper_add_ctest(il_opt_mem2reg_diamond
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_diamond.in.il
    -DGOLDEN=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_diamond.opt.il
    -DPASSES=mem2reg
    -P ${_VIPER_GOLDEN_DIR}/il_opt/check_opt.cmake)

  viper_add_ctest(il_opt_mem2reg_nested
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_nested.in.il
    -DGOLDEN=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_nested.opt.il
    -DPASSES=mem2reg
    -P ${_VIPER_GOLDEN_DIR}/il_opt/check_opt.cmake)

  viper_add_ctest(il_opt_mem2reg_loop
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_loop.in.il
    -DGOLDEN=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_loop.opt.il
    -DPASSES=mem2reg
    -P ${_VIPER_GOLDEN_DIR}/il_opt/check_opt.cmake)

  viper_add_ctest(il_opt_mem2reg_multi
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_multi.in.il
    -DGOLDEN=${_VIPER_GOLDEN_DIR}/il_opt/mem2reg_multi.opt.il
    -DPASSES=mem2reg
    -P ${_VIPER_GOLDEN_DIR}/il_opt/check_opt.cmake)

  file(READ ${_VIPER_GOLDEN_DIR}/quickstart.stdout EXPECT_QUICKSTART)
  string(STRIP "${EXPECT_QUICKSTART}" EXPECT_QUICKSTART)
  viper_add_ctest(il_quickstart_example
    ${CMAKE_COMMAND}
    -DILC=${BASIC_ILC}
    -DIL_FILE=${_VIPER_GOLDEN_DIR}/quickstart.il
    "-DEXPECT=${EXPECT_QUICKSTART}"
    -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)
endfunction()
