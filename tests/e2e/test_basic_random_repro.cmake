## File: tests/e2e/test_basic_random_repro.cmake
## Purpose: Ensure BASIC random sequence reproduces deterministic values.
## Key invariants: VM output matches expected random numbers.
## Ownership/Lifetime: Invoked by CTest.
## Links: docs/codemap.md

# Use a unique filename to avoid collisions when tests run in parallel.
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/basic_random_repro.out.txt")
execute_process(COMMAND ${ILC} front basic -run ${SRC_DIR}/examples/basic/random_repro.bas
                OUTPUT_FILE ${OUT_FILE} RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "execution failed")
endif()
file(READ ${OUT_FILE} R)
if(NOT R STREQUAL "0.345001\n0.752709\n0.795745\n")
  message(FATAL_ERROR "unexpected output: ${R}")
endif()
