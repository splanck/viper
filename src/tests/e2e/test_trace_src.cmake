if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
if (NOT DEFINED GOLDEN)
    message(FATAL_ERROR "GOLDEN not set")
endif ()
set(TRACE_BEFORE trace_before.txt)
set(TRACE_AFTER trace_after.txt)

execute_process(
        COMMAND ${ILC} front basic -run ${BAS_FILE} --trace=src
        ERROR_FILE ${TRACE_BEFORE}
        RESULT_VARIABLE r_before)
if (NOT r_before EQUAL 0)
    message(FATAL_ERROR "execution failed (uncached)")
endif ()

execute_process(
        COMMAND ${ILC} front basic -run ${BAS_FILE} --trace=src
        ERROR_FILE ${TRACE_AFTER}
        RESULT_VARIABLE r_after)
if (NOT r_after EQUAL 0)
    message(FATAL_ERROR "execution failed (cached)")
endif ()

file(READ ${TRACE_BEFORE} OUT_BEFORE)
file(READ ${TRACE_AFTER} OUT_AFTER)
file(READ ${GOLDEN} EXP)

if (NOT OUT_BEFORE STREQUAL EXP)
    message(FATAL_ERROR "trace mismatch before optimization")
endif ()

if (NOT OUT_AFTER STREQUAL EXP)
    message(FATAL_ERROR "trace mismatch after optimization")
endif ()

if (NOT OUT_BEFORE STREQUAL OUT_AFTER)
    message(FATAL_ERROR "trace output changed between runs")
endif ()

file(REMOVE ${TRACE_BEFORE} ${TRACE_AFTER})
