if(NOT DEFINED VIPER_EXE)
    message(FATAL_ERROR "VIPER_EXE must be provided")
endif()

if(NOT DEFINED TEST_FILE)
    message(FATAL_ERROR "TEST_FILE must be provided")
endif()

if(NOT DEFINED OUT_EXE)
    message(FATAL_ERROR "OUT_EXE must be provided")
endif()

execute_process(
        COMMAND "${VIPER_EXE}" build "${TEST_FILE}" -o "${OUT_EXE}" --quiet-warnings
        RESULT_VARIABLE _build_rc
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err)

if(NOT _build_rc EQUAL 0)
    message(FATAL_ERROR "native build failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif()

execute_process(
        COMMAND "${OUT_EXE}"
        RESULT_VARIABLE _run_rc
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err)

if(NOT _run_rc EQUAL 0)
    message(FATAL_ERROR "native run failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif()

if(NOT _run_out MATCHES "RESULT: ok")
    message(FATAL_ERROR "native run did not report success\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif()
