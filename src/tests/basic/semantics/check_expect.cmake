# File: tests/basic/semantics/check_expect.cmake
# Purpose: Run BASIC frontend and compare stdout to expected output.
if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
if (NOT DEFINED EXPECT_FILE)
    message(FATAL_ERROR "EXPECT_FILE not set")
endif ()
if (DEFINED STDIN_FILE)
    execute_process(
            COMMAND ${ILC} front basic -run ${BAS_FILE}
            RESULT_VARIABLE RES
            OUTPUT_VARIABLE OUT
            ERROR_VARIABLE ERR
            INPUT_FILE ${STDIN_FILE})
else ()
    execute_process(
            COMMAND ${ILC} front basic -run ${BAS_FILE}
            RESULT_VARIABLE RES
            OUTPUT_VARIABLE OUT
            ERROR_VARIABLE ERR)
endif ()
if (NOT RES EQUAL 0)
    message(FATAL_ERROR "unexpected exit status: ${RES}. stderr: ${ERR}")
endif ()
file(READ ${EXPECT_FILE} EXPECT_CONTENT)
string(REPLACE "\r\n" "\n" EXPECT_CONTENT "${EXPECT_CONTENT}")
string(REGEX REPLACE "\n+$" "" EXPECT_CONTENT "${EXPECT_CONTENT}")
string(REPLACE "\r\n" "\n" OUT "${OUT}")
string(REGEX REPLACE "\n+$" "" OUT "${OUT}")
if (NOT OUT STREQUAL EXPECT_CONTENT)
    message(FATAL_ERROR "stdout mismatch\nexpected: '${EXPECT_CONTENT}'\nactual: '${OUT}'")
endif ()
