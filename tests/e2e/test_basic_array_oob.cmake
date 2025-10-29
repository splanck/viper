## File: tests/e2e/test_basic_array_oob.cmake
## Purpose: Ensure array bounds violations trigger runtime panic with message.
## Key invariants: Program exits non-zero and reports rt_arr_i32 panic text.
## Ownership/Lifetime: Invoked by CTest via CMake script mode.
## Links: docs/codemap.md

if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
if (NOT DEFINED EXPECT_SUBSTR)
    message(FATAL_ERROR "EXPECT_SUBSTR not set")
endif ()

set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/basic_array_oob.stdout.txt")
set(ERR_FILE "${CMAKE_CURRENT_BINARY_DIR}/basic_array_oob.stderr.txt")
execute_process(
        COMMAND ${ILC} front basic -run ${BAS_FILE}
        OUTPUT_FILE ${OUT_FILE}
        ERROR_FILE ${ERR_FILE}
        RESULT_VARIABLE res)
if (res EQUAL 0)
    message(FATAL_ERROR "expected non-zero exit")
endif ()

file(READ ${ERR_FILE} ERR_RAW)
string(STRIP "${ERR_RAW}" ERR)
string(FIND "${ERR}" "${EXPECT_SUBSTR}" FOUND_INDEX)
if (FOUND_INDEX EQUAL -1)
    message(FATAL_ERROR "panic message mismatch: ${ERR}")
endif ()
