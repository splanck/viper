if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED IL_FILE)
    message(FATAL_ERROR "IL_FILE not set")
endif ()

execute_process(
        COMMAND ${ILC} -run ${IL_FILE}
        RESULT_VARIABLE res
        OUTPUT_VARIABLE ignored_stdout
        ERROR_VARIABLE stderr
)

if (res EQUAL 0)
    message(FATAL_ERROR "expected non-zero exit")
endif ()

string(STRIP "${stderr}" stderr_stripped)
string(REGEX MATCH "^Trap @([^#]+)#([0-9]+) line (-1|[0-9]+): Overflow \\(code=0\\): (.+)$" _match "${stderr_stripped}")
if (NOT _match)
    message(FATAL_ERROR "unexpected trap diagnostic: ${stderr_stripped}")
endif ()

if (NOT CMAKE_MATCH_1 STREQUAL "main")
    message(FATAL_ERROR "unexpected function name: ${CMAKE_MATCH_1}")
endif ()
if (NOT CMAKE_MATCH_2 STREQUAL "0")
    message(FATAL_ERROR "unexpected instruction index: ${CMAKE_MATCH_2}")
endif ()
if (NOT CMAKE_MATCH_3)
    message(FATAL_ERROR "missing source line in diagnostic")
endif ()

set(detail "${CMAKE_MATCH_4}")
if (detail STREQUAL "")
    message(FATAL_ERROR "expected overflow message detail")
endif ()
if (NOT detail MATCHES "overflow")
    message(FATAL_ERROR "unexpected overflow detail: ${detail}")
endif ()
if (NOT detail MATCHES "iadd\\.ovf")
    message(FATAL_ERROR "missing opcode mnemonic in overflow detail: ${detail}")
endif ()
