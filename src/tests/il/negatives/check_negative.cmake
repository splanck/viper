if (NOT DEFINED IL_VERIFY)
    message(FATAL_ERROR "IL_VERIFY not set")
endif ()
if (NOT DEFINED FILE)
    message(FATAL_ERROR "FILE not set")
endif ()
if (NOT DEFINED EXPECT_FILE)
    message(FATAL_ERROR "EXPECT_FILE not set")
endif ()

file(READ "${EXPECT_FILE}" _expect_raw)
string(STRIP "${_expect_raw}" EXPECT_TEXT)
# Normalize Windows line endings in expected text
string(REPLACE "\r\n" "\n" EXPECT_TEXT "${EXPECT_TEXT}")
string(REPLACE "\r" "\n" EXPECT_TEXT "${EXPECT_TEXT}")

execute_process(
        COMMAND ${IL_VERIFY} ${FILE}
        RESULT_VARIABLE res
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
)

if (res EQUAL 0)
    message(FATAL_ERROR "expected il-verify to fail for ${FILE}")
endif ()

set(full_output "${out}${err}")
# Normalize Windows line endings in output
string(REPLACE "\r\n" "\n" full_output "${full_output}")
string(REPLACE "\r" "\n" full_output "${full_output}")
string(FIND "${full_output}" "${EXPECT_TEXT}" match_index)
if (match_index EQUAL -1)
    message(FATAL_ERROR "expected message not found: ${EXPECT_TEXT}\n${full_output}")
endif ()
