if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
if (NOT DEFINED GOLDEN)
    message(FATAL_ERROR "GOLDEN not set")
endif ()

# Optional ARGS as a CMake list (each element is one argument).
set(_ARGS_LIST ${ARGS})

get_filename_component(_BASENAME "${BAS_FILE}" NAME_WE)
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${_BASENAME}.out.txt")

set(_RUN_ARGS front basic -run ${BAS_FILE})
if (DEBUG_VM)
    list(APPEND _RUN_ARGS --debug-vm)
endif ()
if (_ARGS_LIST)
    list(APPEND _RUN_ARGS -- ${_ARGS_LIST})
endif ()

execute_process(
        COMMAND ${ILC} ${_RUN_ARGS}
        OUTPUT_FILE ${OUT_FILE}
        RESULT_VARIABLE r)

if (NOT r EQUAL 0)
    message(FATAL_ERROR "execution failed")
endif ()

file(READ ${OUT_FILE} OUT)
file(READ ${GOLDEN} EXP)
if (NOT OUT STREQUAL EXP)
    message(FATAL_ERROR "stdout mismatch")
endif ()
