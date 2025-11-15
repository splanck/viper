# File: tests/basic/semantics/check_ok.cmake
# Purpose: Verify BASIC file compiles without diagnostics.
if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
execute_process(COMMAND ${ILC} front basic -emit-il ${BAS_FILE} RESULT_VARIABLE res ERROR_VARIABLE err)
if (NOT res EQUAL 0)
    message(FATAL_ERROR "unexpected error: ${err}")
endif ()
