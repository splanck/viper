# SPDX-License-Identifier: GPL-3.0-only
# Verify that the high-level source compiler driver keeps the fast default path
# for `zanna run` while preserving explicit project optimization choices.

if (NOT DEFINED ZANNA_EXE)
    message(FATAL_ERROR "ZANNA_EXE must be provided")
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
        COMMAND "${ZANNA_EXE}" run "${_implicit_dir}" --time-compile --quiet-warnings
        RESULT_VARIABLE _run_result
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err)
if (NOT _run_result EQUAL 0)
    message(FATAL_ERROR "implicit zanna run failed:\nstdout=${_run_out}\nstderr=${_run_err}")
endif ()
if (NOT _run_err MATCHES "\\[time-compile\\] project-resolve")
    message(FATAL_ERROR "implicit zanna run did not report project resolution timing:\n${_run_err}")
endif ()
if (_run_err MATCHES "\\[time-compile\\] zia\\.optimize")
    message(FATAL_ERROR "implicit zanna run unexpectedly optimized by default:\n${_run_err}")
endif ()

execute_process(
        COMMAND "${ZANNA_EXE}" build "${_implicit_dir}" -o "${TEST_WORK_DIR}/implicit.il"
        --time-compile --quiet-warnings
        RESULT_VARIABLE _build_result
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err)
if (NOT _build_result EQUAL 0)
    message(FATAL_ERROR "implicit zanna build failed:\nstdout=${_build_out}\nstderr=${_build_err}")
endif ()
if (NOT _build_err MATCHES "\\[time-compile\\] zia\\.optimize")
    message(FATAL_ERROR "implicit zanna build did not use the balanced optimization default:\n${_build_err}")
endif ()
if (_build_err MATCHES "\\[pass ")
    message(FATAL_ERROR "--time-compile unexpectedly enabled detailed pass statistics:\n${_build_err}")
endif ()

execute_process(
        COMMAND "${ZANNA_EXE}" build "${_implicit_dir}" -o "${TEST_WORK_DIR}/implicit_stats.il"
        --pass-stats --quiet-warnings
        RESULT_VARIABLE _stats_result
        OUTPUT_VARIABLE _stats_out
        ERROR_VARIABLE _stats_err)
if (NOT _stats_result EQUAL 0)
    message(FATAL_ERROR "implicit zanna build with --pass-stats failed:\nstdout=${_stats_out}\nstderr=${_stats_err}")
endif ()
if (NOT _stats_err MATCHES "\\[pass ")
    message(FATAL_ERROR "--pass-stats did not report optimizer pass statistics:\n${_stats_err}")
endif ()

set(_explicit_dir "${TEST_WORK_DIR}/explicit")
file(MAKE_DIRECTORY "${_explicit_dir}")
file(WRITE "${_explicit_dir}/main.zia" "module main;\nfunc start() {}\n")
file(WRITE "${_explicit_dir}/zanna.project"
        "project explicit\n"
        "version 0.1.0\n"
        "lang zia\n"
        "entry main.zia\n"
        "profile balanced\n")

execute_process(
        COMMAND "${ZANNA_EXE}" run "${_explicit_dir}" --time-compile --quiet-warnings
        RESULT_VARIABLE _explicit_run_result
        OUTPUT_VARIABLE _explicit_run_out
        ERROR_VARIABLE _explicit_run_err)
if (NOT _explicit_run_result EQUAL 0)
    message(FATAL_ERROR "explicit-profile zanna run failed:\nstdout=${_explicit_run_out}\nstderr=${_explicit_run_err}")
endif ()
if (NOT _explicit_run_err MATCHES "\\[time-compile\\] zia\\.optimize")
    message(FATAL_ERROR "explicit-profile zanna run did not preserve manifest optimization:\n${_explicit_run_err}")
endif ()

set(_batch_a "${TEST_WORK_DIR}/batch_a")
set(_batch_b "${TEST_WORK_DIR}/batch_b")
set(_batch_out "${TEST_WORK_DIR}/batch_out")
file(MAKE_DIRECTORY "${_batch_a}" "${_batch_b}")
file(WRITE "${_batch_a}/main.zia" "module main;\nfunc start() {}\n")
file(WRITE "${_batch_b}/main.zia" "module main;\nfunc start() {}\n")

execute_process(
        COMMAND "${ZANNA_EXE}" build-many --output-dir "${_batch_out}" -O0 --fast-link
        "one=${_batch_a}" "two=${_batch_b}"
        RESULT_VARIABLE _batch_result
        OUTPUT_VARIABLE _batch_stdout
        ERROR_VARIABLE _batch_stderr)
if (NOT _batch_result EQUAL 0)
    message(FATAL_ERROR
            "build-many failed:\nstdout=${_batch_stdout}\nstderr=${_batch_stderr}")
endif ()
if (NOT EXISTS "${_batch_out}/one" OR NOT EXISTS "${_batch_out}/two")
    message(FATAL_ERROR "build-many did not emit both named outputs")
endif ()
