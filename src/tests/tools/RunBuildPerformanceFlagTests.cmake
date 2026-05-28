# SPDX-License-Identifier: GPL-3.0-only
# Verify that the high-level source compiler driver keeps the fast default path
# for `viper run` while preserving explicit project optimization choices.

if (NOT DEFINED VIPER_EXE)
    message(FATAL_ERROR "VIPER_EXE must be provided")
endif ()
if (NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR must be provided")
endif ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

set(_implicit_dir "${TEST_WORK_DIR}/implicit")
file(MAKE_DIRECTORY "${_implicit_dir}")
file(WRITE "${_implicit_dir}/main.zia" "module main;\nfunc start() {}\n")

execute_process(
        COMMAND "${VIPER_EXE}" run "${_implicit_dir}" --time-compile --quiet-warnings
        RESULT_VARIABLE _run_result
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err)
if (NOT _run_result EQUAL 0)
    message(FATAL_ERROR "implicit viper run failed:\nstdout=${_run_out}\nstderr=${_run_err}")
endif ()
if (NOT _run_err MATCHES "\\[time-compile\\] project-resolve")
    message(FATAL_ERROR "implicit viper run did not report project resolution timing:\n${_run_err}")
endif ()
if (_run_err MATCHES "\\[time-compile\\] zia\\.optimize")
    message(FATAL_ERROR "implicit viper run unexpectedly optimized by default:\n${_run_err}")
endif ()

execute_process(
        COMMAND "${VIPER_EXE}" build "${_implicit_dir}" -o "${TEST_WORK_DIR}/implicit.il"
        --time-compile --quiet-warnings
        RESULT_VARIABLE _build_result
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err)
if (NOT _build_result EQUAL 0)
    message(FATAL_ERROR "implicit viper build failed:\nstdout=${_build_out}\nstderr=${_build_err}")
endif ()
if (NOT _build_err MATCHES "\\[time-compile\\] zia\\.optimize")
    message(FATAL_ERROR "implicit viper build did not use the balanced optimization default:\n${_build_err}")
endif ()
if (_build_err MATCHES "\\[pass ")
    message(FATAL_ERROR "--time-compile unexpectedly enabled detailed pass statistics:\n${_build_err}")
endif ()

execute_process(
        COMMAND "${VIPER_EXE}" build "${_implicit_dir}" -o "${TEST_WORK_DIR}/implicit_stats.il"
        --pass-stats --quiet-warnings
        RESULT_VARIABLE _stats_result
        OUTPUT_VARIABLE _stats_out
        ERROR_VARIABLE _stats_err)
if (NOT _stats_result EQUAL 0)
    message(FATAL_ERROR "implicit viper build with --pass-stats failed:\nstdout=${_stats_out}\nstderr=${_stats_err}")
endif ()
if (NOT _stats_err MATCHES "\\[pass ")
    message(FATAL_ERROR "--pass-stats did not report optimizer pass statistics:\n${_stats_err}")
endif ()

set(_explicit_dir "${TEST_WORK_DIR}/explicit")
file(MAKE_DIRECTORY "${_explicit_dir}")
file(WRITE "${_explicit_dir}/main.zia" "module main;\nfunc start() {}\n")
file(WRITE "${_explicit_dir}/viper.project"
        "project explicit\n"
        "version 0.1.0\n"
        "lang zia\n"
        "entry main.zia\n"
        "profile balanced\n")

execute_process(
        COMMAND "${VIPER_EXE}" run "${_explicit_dir}" --time-compile --quiet-warnings
        RESULT_VARIABLE _explicit_run_result
        OUTPUT_VARIABLE _explicit_run_out
        ERROR_VARIABLE _explicit_run_err)
if (NOT _explicit_run_result EQUAL 0)
    message(FATAL_ERROR "explicit-profile viper run failed:\nstdout=${_explicit_run_out}\nstderr=${_explicit_run_err}")
endif ()
if (NOT _explicit_run_err MATCHES "\\[time-compile\\] zia\\.optimize")
    message(FATAL_ERROR "explicit-profile viper run did not preserve manifest optimization:\n${_explicit_run_err}")
endif ()
