if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED BAS_FILE)
    message(FATAL_ERROR "BAS_FILE not set")
endif ()
if (NOT DEFINED EXPECT)
    set(EXPECT "")
endif ()

set(_expect_exit_zero OFF)
if (DEFINED EXPECT_EXIT_ZERO)
    string(TOUPPER "${EXPECT_EXIT_ZERO}" _expect_exit_zero_upper)
    if (_expect_exit_zero_upper STREQUAL "TRUE" OR
            _expect_exit_zero_upper STREQUAL "ON" OR
            _expect_exit_zero_upper STREQUAL "1")
        set(_expect_exit_zero ON)
    endif ()
endif ()

# For Phase 2 namespace semantics tests, disable runtime namespaces via CLI flag.
set(_ns_flag "")
if (BAS_FILE MATCHES "/basic/namespaces_phase2/")
    set(_ns_flag "--no-runtime-namespaces")
endif ()

execute_process(COMMAND ${ILC} front basic -emit-il ${BAS_FILE} ${_ns_flag}
        RESULT_VARIABLE res OUTPUT_VARIABLE out ERROR_VARIABLE out)

if (_expect_exit_zero)
    if (NOT res EQUAL 0)
        message(FATAL_ERROR "expected zero exit")
    endif ()
else ()
    if (res EQUAL 0)
        message(FATAL_ERROR "expected non-zero exit")
    endif ()
endif ()

if (NOT EXPECT STREQUAL "")
    if (NOT out MATCHES "${EXPECT}")
        message(FATAL_ERROR "expected message not found: ${EXPECT}\\n${out}")
    endif ()
endif ()
