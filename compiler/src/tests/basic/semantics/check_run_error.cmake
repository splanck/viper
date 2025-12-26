# File: tests/basic/semantics/check_run_error.cmake
# Purpose: Execute BASIC program expecting runtime failure with specific diagnostics.
if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
if (NOT DEFINED EXPECT)
    message(FATAL_ERROR "EXPECT not set")
endif ()
if (NOT DEFINED EXPECT_STATUS)
    set(EXPECT_STATUS 1)
endif ()
set(_command ${ILC} front basic -run ${BAS_FILE})
if (DEFINED STDIN_FILE)
    execute_process(
            COMMAND ${_command}
            RESULT_VARIABLE RES
            OUTPUT_VARIABLE OUT
            ERROR_VARIABLE ERR
            INPUT_FILE ${STDIN_FILE})
else ()
    execute_process(
            COMMAND ${_command}
            RESULT_VARIABLE RES
            OUTPUT_VARIABLE OUT
            ERROR_VARIABLE ERR)
endif ()
if (NOT RES EQUAL EXPECT_STATUS)
    message(FATAL_ERROR "unexpected exit status: expected ${EXPECT_STATUS}, got ${RES}. stderr: ${ERR}")
endif ()
string(REPLACE "\r\n" "\n" ERR "${ERR}")
string(REGEX REPLACE "\n+$" "" ERR "${ERR}")
if (NOT ERR MATCHES "${EXPECT}")
    message(FATAL_ERROR "stderr mismatch\nexpected pattern: '${EXPECT}'\nactual: '${ERR}'")
endif ()
