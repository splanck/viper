# File: tests/e2e/test_break_src_line.cmake
# Purpose: Verify VM halts at source line breakpoints.
# Key invariants: [BREAK] emitted before program output.
# Ownership/Lifetime: Temporary files created in build dir.
# Links: docs/testing.md

if(NOT DEFINED ILC)
  message(FATAL_ERROR "ILC not set")
endif()
if(NOT DEFINED BAS_FILE)
  message(FATAL_ERROR "BAS_FILE not set")
endif()
if(NOT DEFINED GOLDEN)
  message(FATAL_ERROR "GOLDEN not set")
endif()

set(dbg "${CMAKE_BINARY_DIR}/break_src_line.dbg")
file(WRITE ${dbg} "continue\n")
execute_process(COMMAND ${ILC} front basic -run ${BAS_FILE} --break ${BAS_FILE}:2 --debug-cmds ${dbg}
                OUTPUT_FILE out.txt ERROR_FILE err.txt RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "execution failed")
endif()
file(READ err.txt ERR)
set(expect "[BREAK] fn=@main src=${BAS_FILE}:2 reason=src-line\n")
if(NOT ERR STREQUAL "${expect}")
  message(FATAL_ERROR "unexpected break output: ${ERR}")
endif()
file(READ out.txt OUT)
file(READ ${GOLDEN} EXP)
if(NOT OUT STREQUAL EXP)
  message(FATAL_ERROR "stdout mismatch")
endif()
