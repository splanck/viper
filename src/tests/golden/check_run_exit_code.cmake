# SPDX-License-Identifier: GPL-3.0-only
# File: tests/golden/check_run_exit_code.cmake
# Purpose: Run a Viper program and verify exit code/output for EndProgram tests.

if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (DEFINED BAS_FILE AND DEFINED IL_FILE)
    message(FATAL_ERROR "Specify only one of BAS_FILE or IL_FILE")
endif ()
if (NOT DEFINED BAS_FILE AND NOT DEFINED IL_FILE)
    message(FATAL_ERROR "BAS_FILE or IL_FILE not set")
endif ()
if (NOT DEFINED EXPECT_EXIT)
    message(FATAL_ERROR "EXPECT_EXIT not set")
endif ()

set(_cmd ${ILC})
if (DEFINED BAS_FILE)
    list(APPEND _cmd front basic -run ${BAS_FILE})
elseif (DEFINED IL_FILE)
    list(APPEND _cmd -run ${IL_FILE})
endif ()

execute_process(
        COMMAND ${_cmd}
        RESULT_VARIABLE res
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)

if (NOT res EQUAL ${EXPECT_EXIT})
    message(FATAL_ERROR "expected exit ${EXPECT_EXIT} got ${res}\nstdout: ${out}\nstderr: ${err}")
endif ()

if (DEFINED EXPECT_STDOUT)
    string(REPLACE "\r\n" "\n" out "${out}")
    string(REGEX REPLACE "\n+$" "" out "${out}")
    if (NOT out STREQUAL "${EXPECT_STDOUT}")
        message(FATAL_ERROR "stdout mismatch: expected '${EXPECT_STDOUT}' got '${out}'")
    endif ()
endif ()
