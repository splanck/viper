if (NOT DEFINED VIPER_EXE)
    message(FATAL_ERROR "VIPER_EXE must be provided")
endif ()

if (NOT DEFINED TEST_FILE)
    message(FATAL_ERROR "TEST_FILE must be provided")
endif ()

if (NOT DEFINED OUT_EXE)
    message(FATAL_ERROR "OUT_EXE must be provided")
endif ()

# Optional: OPT_FLAG (e.g. -O0 / -O2) pins the optimization level so ABI
# regressions can be exercised at both ends of the pipeline.
set(_build_args "${TEST_FILE}" -o "${OUT_EXE}" --quiet-warnings)
if (DEFINED OPT_FLAG AND NOT OPT_FLAG STREQUAL "")
    list(APPEND _build_args "${OPT_FLAG}")
endif ()

execute_process(
        COMMAND "${VIPER_EXE}" build ${_build_args}
        RESULT_VARIABLE _build_rc
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err)

if (NOT _build_rc EQUAL 0)
    message(FATAL_ERROR "native build failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

execute_process(
        COMMAND "${OUT_EXE}"
        TIMEOUT 10
        RESULT_VARIABLE _run_rc
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err)

if (NOT _run_rc EQUAL 0)
    if (_run_out MATCHES "RESULT: ok")
        return()
    endif ()
    message(FATAL_ERROR "native run failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()

if (NOT _run_out MATCHES "RESULT: ok")
    message(FATAL_ERROR "native run did not report success\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()
