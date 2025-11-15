## File: tests/e2e/test_basic_math_phase1.cmake
## Purpose: Validate BASIC math example (phase1) output matches golden file.
## Key invariants: VM execution output equals expected golden output.
## Ownership/Lifetime: Invoked by CTest.
## Links: docs/codemap.md

if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
if (NOT DEFINED GOLDEN)
    message(FATAL_ERROR "GOLDEN not set")
endif ()
# Use a unique filename to avoid collisions when tests run in parallel.
get_filename_component(_BASENAME "${BAS_FILE}" NAME_WE)
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${_BASENAME}.out.txt")
execute_process(COMMAND ${ILC} front basic -run ${BAS_FILE} OUTPUT_FILE ${OUT_FILE} RESULT_VARIABLE r)
if (NOT r EQUAL 0)
    message(FATAL_ERROR "execution failed")
endif ()
file(READ ${OUT_FILE} OUT)
file(READ ${GOLDEN} EXP)
if (NOT OUT STREQUAL EXP)
    message(FATAL_ERROR "stdout mismatch")
endif ()
