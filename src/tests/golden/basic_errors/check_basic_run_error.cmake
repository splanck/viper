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
# Normalize Windows line endings
string(REPLACE "\r\n" "\n" err "${err}")
string(REPLACE "\r" "\n" err "${err}")
if (res EQUAL 0)
    message(FATAL_ERROR "expected non-zero exit")
endif ()
if (NOT err MATCHES "${EXPECT}")
    message(FATAL_ERROR "expected error message not found: ${EXPECT}\n${err}")
endif ()
