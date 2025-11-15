if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
if (NOT DEFINED GOLDEN)
    message(FATAL_ERROR "GOLDEN not set")
endif ()

# Optional ARGS as a CMake list (each element is one argument)
set(_ARGS_LIST ${ARGS})

get_filename_component(_BASENAME "${BAS_FILE}" NAME_WE)
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${_BASENAME}.out.txt")

if (_ARGS_LIST)
    execute_process(
            COMMAND ${ILC} front basic -run ${BAS_FILE} -- ${_ARGS_LIST}
            OUTPUT_FILE ${OUT_FILE}
            RESULT_VARIABLE r)
else ()
    execute_process(
            COMMAND ${ILC} front basic -run ${BAS_FILE}
            OUTPUT_FILE ${OUT_FILE}
            RESULT_VARIABLE r)
endif ()

if (NOT r EQUAL 0)
    message(FATAL_ERROR "execution failed")
endif ()

file(READ ${OUT_FILE} OUT)
file(READ ${GOLDEN} EXP)
if (NOT OUT STREQUAL EXP)
    message(FATAL_ERROR "stdout mismatch")
endif ()

