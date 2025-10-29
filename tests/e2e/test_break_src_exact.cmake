if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED SRC_FILE)
    message(FATAL_ERROR "SRC_FILE not set")
endif ()
if (NOT DEFINED GOLDEN)
    message(FATAL_ERROR "GOLDEN not set")
endif ()
if (NOT DEFINED LINE)
    message(FATAL_ERROR "LINE not set")
endif ()
if (NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT not set")
endif ()
get_filename_component(SRC_NAME ${SRC_FILE} NAME)
set(BREAK_FILE ${ROOT}/break.txt)

execute_process(COMMAND ${ILC} -run ${SRC_FILE} --break ${SRC_FILE}:${LINE}
        ERROR_FILE ${BREAK_FILE}
        RESULT_VARIABLE r
        WORKING_DIRECTORY ${ROOT})
if (NOT r EQUAL 10)
    message(FATAL_ERROR "expected breakpoint")
endif ()
file(READ ${BREAK_FILE} OUT)
file(READ ${GOLDEN} EXP)
string(REPLACE "\\" "/" OUT "${OUT}")
string(REPLACE "\\" "/" EXP "${EXP}")
if (NOT OUT STREQUAL EXP)
    message(FATAL_ERROR "break output mismatch (full path)")
endif ()

execute_process(COMMAND ${ILC} -run ${SRC_FILE} --break ${SRC_NAME}:${LINE}
        ERROR_FILE ${BREAK_FILE}
        RESULT_VARIABLE r
        WORKING_DIRECTORY ${ROOT})
if (NOT r EQUAL 10)
    message(FATAL_ERROR "expected breakpoint")
endif ()
file(READ ${BREAK_FILE} OUT)
string(REPLACE "\\" "/" OUT "${OUT}")
if (NOT OUT STREQUAL EXP)
    message(FATAL_ERROR "break output mismatch (basename)")
endif ()

