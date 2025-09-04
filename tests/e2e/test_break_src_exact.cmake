# File: tests/e2e/test_break_src_exact.cmake
# Purpose: Verify source line breakpoints halt execution.
# Key invariants: ilc returns 10 and prints a single BREAK line.
# Ownership/Lifetime: N/A.
# Links: docs/testing.md
if(NOT DEFINED ILC)
  message(FATAL_ERROR "ILC not set")
endif()
if(NOT DEFINED SRC_DIR)
  message(FATAL_ERROR "SRC_DIR not set")
endif()
set(PROG ${SRC_DIR}/tests/e2e/BreakSrcExact.bas)
set(GOLDEN ${SRC_DIR}/tests/goldens/break_src_exact.out)
execute_process(COMMAND ${ILC} front basic -run ${PROG} --break-src ${PROG}:4 ERROR_FILE out.txt RESULT_VARIABLE r)
if(NOT r EQUAL 10)
  message(FATAL_ERROR "execution failed")
endif()
file(READ out.txt OUT)
file(READ ${GOLDEN} EXP)
if(NOT OUT STREQUAL EXP)
  message(FATAL_ERROR "break output mismatch")
endif()
