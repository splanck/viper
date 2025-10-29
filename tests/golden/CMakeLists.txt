# SPDX-License-Identifier: MIT
# File: tests/golden/CMakeLists.txt
# Purpose: Golden tests for BASIC diagnostics and IL optimizations.

function(viper_add_golden_suite_tests)
    if (TEST basic_error_unknown_var)
        return()
    endif ()

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

    file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/exit_outside_loop.stderr EXPECT_EXIT_OUTSIDE_LOOP)
    string(STRIP "${EXPECT_EXIT_OUTSIDE_LOOP}" EXPECT_EXIT_OUTSIDE_LOOP)
    viper_add_ctest(basic_error_exit_outside_loop
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/exit_outside_loop.bas
            "-DEXPECT=${EXPECT_EXIT_OUTSIDE_LOOP}"
            -P ${_VIPER_GOLDEN_DIR}/basic_errors/check_error.cmake)

    file(READ ${_VIPER_GOLDEN_DIR}/basic_errors/exit_mismatch.stderr EXPECT_EXIT_MISMATCH)
    string(STRIP "${EXPECT_EXIT_MISMATCH}" EXPECT_EXIT_MISMATCH)
    viper_add_ctest(basic_error_exit_mismatch
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_errors/exit_mismatch.bas
            "-DEXPECT=${EXPECT_EXIT_MISMATCH}"
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
            -DEXPECT_EXIT_ZERO=TRUE
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

    viper_add_ctest(vm_trap_overflow_message
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${_VIPER_GOLDEN_DIR}/vm_trap_overflow.il
            -P ${_VIPER_GOLDEN_DIR}/check_vm_trap_overflow.cmake)

    set(NUMERICS_IL_DIR ${_VIPER_GOLDEN_DIR}/numerics_il)

    viper_add_ctest(numerics_il_i16_add_overflow
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${NUMERICS_IL_DIR}/i16_add_overflow.il
            -DEXPECT=Overflow
            -P ${_VIPER_GOLDEN_DIR}/check_vm_trap_loc.cmake)

    viper_add_ctest(numerics_il_int_div_min_overflow
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${NUMERICS_IL_DIR}/int_div_min_overflow.il
            -DEXPECT=Overflow
            -P ${_VIPER_GOLDEN_DIR}/check_vm_trap_loc.cmake)

    viper_add_ctest(numerics_il_i16_mul_ok
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${NUMERICS_IL_DIR}/i16_mul_ok.il
            -DEXPECT=30000
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

    string(JOIN "\n" EXPECT_NUMERICS_MOD "-1" "1" "-1")
    viper_add_ctest(numerics_il_mod_sign
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${NUMERICS_IL_DIR}/mod_sign.il
            "-DEXPECT=${EXPECT_NUMERICS_MOD}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

    set(ERRORS_MAP_DIR ${_VIPER_GOLDEN_DIR}/errors_map)
    file(READ ${ERRORS_MAP_DIR}/print_kind_expected.out EXPECT_ERRORS_MAP_PRINT_KIND)
    string(STRIP "${EXPECT_ERRORS_MAP_PRINT_KIND}" EXPECT_ERRORS_MAP_PRINT_KIND)
    viper_add_ctest(errors_map_print_kind_from_handler
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${ERRORS_MAP_DIR}/print_kind_from_handler.il
            "-DEXPECT=${EXPECT_ERRORS_MAP_PRINT_KIND}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

    file(READ ${ERRORS_MAP_DIR}/div_zero_expected.out EXPECT_ERRORS_MAP_DIV_ZERO)
    string(STRIP "${EXPECT_ERRORS_MAP_DIV_ZERO}" EXPECT_ERRORS_MAP_DIV_ZERO)
    viper_add_ctest(errors_map_div_zero
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${ERRORS_MAP_DIR}/div_zero.il
            "-DEXPECT=${EXPECT_ERRORS_MAP_DIV_ZERO}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

    file(READ ${ERRORS_MAP_DIR}/cast_invalid_expected.out EXPECT_ERRORS_MAP_CAST_INVALID)
    string(STRIP "${EXPECT_ERRORS_MAP_CAST_INVALID}" EXPECT_ERRORS_MAP_CAST_INVALID)
    viper_add_ctest(errors_map_cast_invalid
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${ERRORS_MAP_DIR}/cast_invalid.il
            "-DEXPECT=${EXPECT_ERRORS_MAP_CAST_INVALID}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

    file(READ ${ERRORS_MAP_DIR}/idx_oob_expected.out EXPECT_ERRORS_MAP_IDX_OOB)
    string(STRIP "${EXPECT_ERRORS_MAP_IDX_OOB}" EXPECT_ERRORS_MAP_IDX_OOB)
    viper_add_ctest(errors_map_idx_oob
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${ERRORS_MAP_DIR}/idx_oob.il
            "-DEXPECT=${EXPECT_ERRORS_MAP_IDX_OOB}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

    file(READ ${ERRORS_MAP_DIR}/from_err_fnf_expected.out EXPECT_ERRORS_MAP_FROM_ERR_FNF)
    string(STRIP "${EXPECT_ERRORS_MAP_FROM_ERR_FNF}" EXPECT_ERRORS_MAP_FROM_ERR_FNF)
    viper_add_ctest(errors_map_from_err_fnf
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${ERRORS_MAP_DIR}/from_err_fnf.il
            "-DEXPECT=${EXPECT_ERRORS_MAP_FROM_ERR_FNF}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

    file(READ ${ERRORS_MAP_DIR}/expected.out EXPECT_ERRORS_MAP_FROM_ERR)
    string(STRIP "${EXPECT_ERRORS_MAP_FROM_ERR}" EXPECT_ERRORS_MAP_FROM_ERR)
    viper_add_ctest(errors_map_from_err
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${ERRORS_MAP_DIR}/from_err.il
            "-DEXPECT=${EXPECT_ERRORS_MAP_FROM_ERR}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

    set(EH_RUNTIME_DIR ${_VIPER_GOLDEN_DIR}/eh_runtime)

    file(READ ${EH_RUNTIME_DIR}/div_by_zero_with_handler.out EXPECT_EH_RUNTIME_DIV_ZERO_WITH_HANDLER)
    string(STRIP "${EXPECT_EH_RUNTIME_DIV_ZERO_WITH_HANDLER}" EXPECT_EH_RUNTIME_DIV_ZERO_WITH_HANDLER)
    viper_add_ctest(eh_runtime_div_by_zero_with_handler
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${EH_RUNTIME_DIR}/div_by_zero_with_handler.il
            "-DEXPECT=${EXPECT_EH_RUNTIME_DIV_ZERO_WITH_HANDLER}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

    file(READ ${EH_RUNTIME_DIR}/bounds_with_handler.out EXPECT_EH_RUNTIME_BOUNDS_WITH_HANDLER)
    string(STRIP "${EXPECT_EH_RUNTIME_BOUNDS_WITH_HANDLER}" EXPECT_EH_RUNTIME_BOUNDS_WITH_HANDLER)
    viper_add_ctest(eh_runtime_bounds_with_handler
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${EH_RUNTIME_DIR}/bounds_with_handler.il
            "-DEXPECT=${EXPECT_EH_RUNTIME_BOUNDS_WITH_HANDLER}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)

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

    viper_add_ctest(constfold_add_overflow
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${_VIPER_GOLDEN_DIR}/constfold/constfold_add_overflow.il
            -DGOLDEN=${_VIPER_GOLDEN_DIR}/constfold/constfold_add_overflow.opt.il
            -DEXPECT_NONZERO=1
            -DEXPECT_REGEX=Overflow
            -P ${_VIPER_GOLDEN_DIR}/constfold/check_constfold.cmake)

    viper_add_ctest(constfold_div_by_zero
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${_VIPER_GOLDEN_DIR}/constfold/constfold_div_by_zero.il
            -DGOLDEN=${_VIPER_GOLDEN_DIR}/constfold/constfold_div_by_zero.opt.il
            -DEXPECT_NONZERO=1
            -DEXPECT_REGEX=DivideByZero
            -P ${_VIPER_GOLDEN_DIR}/constfold/check_constfold.cmake)

    viper_add_ctest(constfold_rounding
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${_VIPER_GOLDEN_DIR}/constfold/constfold_rounding.il
            -DGOLDEN=${_VIPER_GOLDEN_DIR}/constfold/constfold_rounding.opt.il
            -DEXPECT_RESULT=0
            -DSKIP_RUNTIME=1
            -P ${_VIPER_GOLDEN_DIR}/constfold/check_constfold.cmake)

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

    viper_add_ctest(basic_fileio_lowering_open_close
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_DIR}/fileio/lowering_open_close.bas
            -DGOLDEN=${_VIPER_GOLDEN_DIR}/fileio/lowering_open_close.il
            -P ${_VIPER_GOLDEN_DIR}/basic_to_il/check_il.cmake)

    viper_add_ctest(basic_fileio_lowering_print_hash_multi
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_DIR}/fileio/lowering_print_hash_multi.bas
            -DGOLDEN=${_VIPER_GOLDEN_DIR}/fileio/lowering_print_hash_multi.il
            -P ${_VIPER_GOLDEN_DIR}/basic_to_il/check_il.cmake)

    viper_add_ctest(basic_to_il_gosub
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic_to_il/gosub.bas
            -DGOLDEN=${_VIPER_GOLDEN_DIR}/basic_to_il/gosub.il
            -P ${_VIPER_GOLDEN_DIR}/basic_to_il/check_il.cmake)

    file(READ ${_VIPER_GOLDEN_DIR}/arrays/assign_alias.stdout EXPECT_ASSIGN_ALIAS)
    string(STRIP "${EXPECT_ASSIGN_ALIAS}" EXPECT_ASSIGN_ALIAS)
    viper_add_ctest(basic_arrays_assign_alias
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_DIR}/arrays/assign_alias.bas
            "-DEXPECT=${EXPECT_ASSIGN_ALIAS}"
            -P ${_VIPER_GOLDEN_DIR}/arrays/check_basic_run_output.cmake)

    file(READ ${_VIPER_GOLDEN_DIR}/arrays/resize_breaks_share.stdout EXPECT_ARRAY_RESIZE_BREAKS_SHARE)
    string(STRIP "${EXPECT_ARRAY_RESIZE_BREAKS_SHARE}" EXPECT_ARRAY_RESIZE_BREAKS_SHARE)
    viper_add_ctest(basic_arrays_resize_breaks_share
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_DIR}/arrays/resize_breaks_share.bas
            "-DEXPECT=${EXPECT_ARRAY_RESIZE_BREAKS_SHARE}"
            -P ${_VIPER_GOLDEN_DIR}/arrays/check_basic_run_output.cmake)

    file(READ ${_VIPER_GOLDEN_DIR}/basic/print_commas.stdout EXPECT_BASIC_PRINT_COMMAS)
    string(STRIP "${EXPECT_BASIC_PRINT_COMMAS}" EXPECT_BASIC_PRINT_COMMAS)
    viper_add_ctest(basic_print_commas_alignment
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_DIR}/basic/print_commas.bas
            "-DEXPECT=${EXPECT_BASIC_PRINT_COMMAS}"
            -P ${_VIPER_GOLDEN_DIR}/arrays/check_basic_run_output.cmake)

    file(READ ${_VIPER_GOLDEN_DIR}/quickstart.stdout EXPECT_QUICKSTART)
    string(STRIP "${EXPECT_QUICKSTART}" EXPECT_QUICKSTART)
    viper_add_ctest(il_quickstart_example
            ${CMAKE_COMMAND}
            -DILC=${BASIC_ILC}
            -DIL_FILE=${_VIPER_GOLDEN_DIR}/quickstart.il
            "-DEXPECT=${EXPECT_QUICKSTART}"
            -P ${_VIPER_GOLDEN_DIR}/check_run_output.cmake)
endfunction()
