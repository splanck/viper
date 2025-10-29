## File: tests/e2e/test_basic_array_sum.cmake
## Purpose: Execute BASIC array fill/sum program and validate stdout.
## Key invariants: VM execution output equals expected sum.
## Ownership/Lifetime: Invoked by CTest via CMake script mode.
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

set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/basic_array_sum.out.txt")
execute_process(
        COMMAND ${ILC} front basic -run ${BAS_FILE}
        OUTPUT_FILE ${OUT_FILE}
        RESULT_VARIABLE res)
if (NOT res EQUAL 0)
    message(FATAL_ERROR "execution failed")
endif ()

file(READ ${OUT_FILE} OUT)
file(READ ${GOLDEN} EXP)
if (NOT OUT STREQUAL EXP)
    message(FATAL_ERROR "stdout mismatch")
endif ()
