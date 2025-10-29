## File: tests/e2e/test_basic_numeric_overflow.cmake
## Purpose: Validate BASIC integer overflow traps while preserving earlier output.
## Key invariants: Program traps with Overflow and prints expected prefix output.
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
if (NOT DEFINED EXPECT_SUBSTR)
    message(FATAL_ERROR "EXPECT_SUBSTR not set")
endif ()
string(REGEX REPLACE "^\"(.*)\"$" "\\1" EXPECT_SUBSTR "${EXPECT_SUBSTR}")

get_filename_component(_TEST_NAME "${BAS_FILE}" NAME_WE)
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${_TEST_NAME}.stdout.txt")
set(ERR_FILE "${CMAKE_CURRENT_BINARY_DIR}/${_TEST_NAME}.stderr.txt")
execute_process(
        COMMAND ${ILC} front basic -run ${BAS_FILE}
        OUTPUT_FILE ${OUT_FILE}
        ERROR_FILE ${ERR_FILE}
        RESULT_VARIABLE res)
if (res EQUAL 0)
    message(FATAL_ERROR "expected non-zero exit")
endif ()

file(READ ${OUT_FILE} OUT_RAW)
file(READ ${GOLDEN} EXP_RAW)
string(STRIP "${OUT_RAW}" OUT)
string(STRIP "${EXP_RAW}" EXP)
if (NOT OUT STREQUAL EXP)
    message(FATAL_ERROR "stdout mismatch")
endif ()

file(READ ${ERR_FILE} ERR_RAW)
string(STRIP "${ERR_RAW}" ERR)
if (NOT ERR MATCHES "${EXPECT_SUBSTR}")
    message(FATAL_ERROR "missing expected trap: ${ERR}")
endif ()
