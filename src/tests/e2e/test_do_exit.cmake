## File: tests/e2e/test_do_exit.cmake
## Purpose: Run BASIC loop control programs and compare stdout to golden outputs.
## Key invariants: Runtime stdout must match the provided golden file byte-for-byte.
## Ownership/Lifetime: Invoked by CTest via CMake script mode for BASIC front-end programs.
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

get_filename_component(_case_stem "${BAS_FILE}" NAME_WE)
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${_case_stem}.out.txt")
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
