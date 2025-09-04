# File: tests/e2e/test_il_opt_equiv.cmake
# Purpose: Verify that il-opt transformations preserve program output.
# Notes: Strips trailing whitespace to avoid platform-specific newlines.
if(NOT DEFINED ILC)
  message(FATAL_ERROR "ILC not set")
endif()
if(NOT DEFINED IL_FILE)
  message(FATAL_ERROR "IL_FILE not set")
endif()
set(OPT_FILE "${CMAKE_CURRENT_BINARY_DIR}/opt.il")
execute_process(
  COMMAND ${ILC} -run ${IL_FILE}
  OUTPUT_VARIABLE before
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE r1)
if(NOT r1 EQUAL 0)
  message(FATAL_ERROR "run before failed")
endif()
execute_process(
  COMMAND ${ILC} il-opt ${IL_FILE} -o ${OPT_FILE} --passes constfold,peephole
  RESULT_VARIABLE r2)
if(NOT r2 EQUAL 0)
  message(FATAL_ERROR "il-opt failed")
endif()
execute_process(
  COMMAND ${ILC} -run ${OPT_FILE}
  OUTPUT_VARIABLE after
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE r3)
if(NOT r3 EQUAL 0)
  message(FATAL_ERROR "run after failed")
endif()
if(NOT before STREQUAL after)
  message("before: ${before}")
  message("after: ${after}")
  message(FATAL_ERROR "stdout mismatch")
endif()
