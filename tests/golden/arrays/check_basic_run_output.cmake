# SPDX-License-Identifier: MIT
# File: tests/golden/arrays/check_basic_run_output.cmake
# Purpose: Run BASIC frontend on a program and check stdout.
if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
if (NOT DEFINED EXPECT)
    message(FATAL_ERROR "EXPECT not set")
endif ()
execute_process(
        COMMAND ${ILC} front basic -run ${BAS_FILE}
        RESULT_VARIABLE res
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
if (NOT res EQUAL 0)
    message(FATAL_ERROR "expected zero exit: ${res} stderr: ${err}")
endif ()
string(REPLACE "\r\n" "\n" out "${out}")
string(REGEX REPLACE "\n+$" "" out "${out}")
if (NOT out STREQUAL "${EXPECT}")
    message(FATAL_ERROR "output mismatch: expected '${EXPECT}' got '${out}'")
endif ()
