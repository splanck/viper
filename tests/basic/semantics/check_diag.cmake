# File: tests/basic/semantics/check_diag.cmake
# Purpose: Run BASIC frontend and compare diagnostics with expected output.
if(NOT DEFINED ILC)
  message(FATAL_ERROR "ILC not set")
endif()
if(NOT DEFINED BAS_FILE)
  message(FATAL_ERROR "BAS_FILE not set")
endif()
if(NOT DEFINED DIAG_FILE)
  message(FATAL_ERROR "DIAG_FILE not set")
endif()
file(READ ${DIAG_FILE} EXPECT_DIAG)
string(REPLACE "\r\n" "\n" EXPECT_DIAG "${EXPECT_DIAG}")
string(REGEX REPLACE "\n+$" "" EXPECT_DIAG "${EXPECT_DIAG}")
execute_process(
  COMMAND ${ILC} front basic -emit-il ${BAS_FILE}
  RESULT_VARIABLE RES
  OUTPUT_VARIABLE OUT
  ERROR_VARIABLE ERR
)
if(NOT RES EQUAL 0)
  message(FATAL_ERROR "expected success (got ${RES}): ${ERR}")
endif()
string(REPLACE "\r\n" "\n" ERR "${ERR}")
string(REGEX REPLACE "\n+$" "" ERR "${ERR}")
if(NOT ERR STREQUAL EXPECT_DIAG)
  message(FATAL_ERROR "diagnostics mismatch\nexpected: '${EXPECT_DIAG}'\nactual: '${ERR}'")
endif()
