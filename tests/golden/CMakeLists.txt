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

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/const_div_zero.stderr EXPECT_CONST_DIV_ZERO)
string(STRIP "${EXPECT_CONST_DIV_ZERO}" EXPECT_CONST_DIV_ZERO)
add_test(NAME basic_error_const_div_zero COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/const_div_zero.bas
  "-DEXPECT=${EXPECT_CONST_DIV_ZERO}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/vm_trap_loc.stderr EXPECT_VM_TRAP_LOC)
string(STRIP "${EXPECT_VM_TRAP_LOC}" EXPECT_VM_TRAP_LOC)
add_test(NAME vm_trap_loc COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DIL_FILE=${CMAKE_CURRENT_SOURCE_DIR}/vm_trap_loc.il
  "-DEXPECT=${EXPECT_VM_TRAP_LOC}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/check_vm_trap_loc.cmake)
