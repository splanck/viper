# SPDX-License-Identifier: MIT
# File: tests/golden/basic_to_il/check_il_contains.cmake
# Purpose: Emit IL for a BASIC file and assert presence/absence of substrings.

if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()

execute_process(
        COMMAND ${ILC} front basic -emit-il ${BAS_FILE}
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        RESULT_VARIABLE res)
if (NOT res EQUAL 0)
    message(FATAL_ERROR "emit-il failed: ${res} stderr: ${err}")
endif ()

# MUST_HAVE: semicolon-separated list of substrings expected to occur
if (DEFINED MUST_HAVE)
    string(REPLACE ";" ";" _must_have "${MUST_HAVE}")
    foreach (needle IN LISTS _must_have)
        if (NOT needle STREQUAL "")
            string(FIND "${out}" "${needle}" pos)
            if (pos EQUAL -1)
                message(FATAL_ERROR "IL does not contain expected token: '${needle}'\n${out}")
            endif ()
        endif ()
    endforeach ()
endif ()

# MUST_NOT_HAVE: semicolon-separated list of substrings expected to be absent
if (DEFINED MUST_NOT_HAVE)
    string(REPLACE ";" ";" _must_not_have "${MUST_NOT_HAVE}")
    foreach (needle IN LISTS _must_not_have)
        if (NOT needle STREQUAL "")
            string(FIND "${out}" "${needle}" pos)
            if (NOT pos EQUAL -1)
                message(FATAL_ERROR "IL unexpectedly contains token: '${needle}'\n${out}")
            endif ()
        endif ()
    endforeach ()
endif ()

