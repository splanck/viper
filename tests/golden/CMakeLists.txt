# Golden tests for BASIC frontend diagnostics.

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/unknown_var.stderr EXPECT_UNKNOWN_VAR)
string(STRIP "${EXPECT_UNKNOWN_VAR}" EXPECT_UNKNOWN_VAR)
add_test(NAME basic_error_unknown_var COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/unknown_var.bas
  "-DEXPECT=${EXPECT_UNKNOWN_VAR}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/mismatched_next.stderr EXPECT_MISMATCHED_NEXT)
string(STRIP "${EXPECT_MISMATCHED_NEXT}" EXPECT_MISMATCHED_NEXT)
add_test(NAME basic_error_mismatched_next COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/mismatched_next.bas
  "-DEXPECT=${EXPECT_MISMATCHED_NEXT}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/bad_goto.stderr EXPECT_BAD_GOTO)
string(STRIP "${EXPECT_BAD_GOTO}" EXPECT_BAD_GOTO)
add_test(NAME basic_error_bad_goto COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/bad_goto.bas
  "-DEXPECT=${EXPECT_BAD_GOTO}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/string_arith.stderr EXPECT_STRING_ARITH)
string(STRIP "${EXPECT_STRING_ARITH}" EXPECT_STRING_ARITH)
add_test(NAME basic_error_string_arith COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/string_arith.bas
  "-DEXPECT=${EXPECT_STRING_ARITH}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/string_cmp_order.stderr EXPECT_STRING_CMP_ORDER)
string(STRIP "${EXPECT_STRING_CMP_ORDER}" EXPECT_STRING_CMP_ORDER)
add_test(NAME basic_error_string_cmp_order COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/string_cmp_order.bas
  "-DEXPECT=${EXPECT_STRING_CMP_ORDER}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/const_div_zero.stderr EXPECT_CONST_DIV_ZERO)
string(STRIP "${EXPECT_CONST_DIV_ZERO}" EXPECT_CONST_DIV_ZERO)
add_test(NAME basic_error_const_div_zero COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/const_div_zero.bas
  "-DEXPECT=${EXPECT_CONST_DIV_ZERO}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/bad_string_intrinsics.stderr EXPECT_BAD_STRING_INTRINSICS)
string(STRIP "${EXPECT_BAD_STRING_INTRINSICS}" EXPECT_BAD_STRING_INTRINSICS)
add_test(NAME basic_error_bad_string_intrinsics COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/bad_string_intrinsics.bas
  "-DEXPECT=${EXPECT_BAD_STRING_INTRINSICS}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/vm_trap_loc.stderr EXPECT_VM_TRAP_LOC)
string(STRIP "${EXPECT_VM_TRAP_LOC}" EXPECT_VM_TRAP_LOC)
add_test(NAME vm_trap_loc COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DIL_FILE=${CMAKE_CURRENT_SOURCE_DIR}/vm_trap_loc.il
  "-DEXPECT=${EXPECT_VM_TRAP_LOC}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/check_vm_trap_loc.cmake)

# IL optimizer golden tests
add_test(NAME il_opt_const_if COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DIL_FILE=${CMAKE_CURRENT_SOURCE_DIR}/il_opt/const_if.in.il
  -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/il_opt/const_if.opt.il
  -P ${CMAKE_CURRENT_SOURCE_DIR}/il_opt/check_opt.cmake)
add_test(NAME il_opt_arith_id COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DIL_FILE=${CMAKE_CURRENT_SOURCE_DIR}/il_opt/arith_id.in.il
  -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/il_opt/arith_id.opt.il
  -P ${CMAKE_CURRENT_SOURCE_DIR}/il_opt/check_opt.cmake)
add_test(NAME il_opt_cbr_same_target COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DIL_FILE=${CMAKE_CURRENT_SOURCE_DIR}/il_opt/cbr_same_target.in.il
  -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/il_opt/cbr_same_target.golden
  -DPASSES=peephole
  -P ${CMAKE_CURRENT_SOURCE_DIR}/il_opt/check_opt.cmake)
set_tests_properties(il_opt_cbr_same_target PROPERTIES LABELS Smoke)
