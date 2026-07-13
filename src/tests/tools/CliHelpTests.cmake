if (NOT DEFINED VIPER_EXE)
    message(FATAL_ERROR "VIPER_EXE is required")
endif ()

function(run_help_case name)
    execute_process(
            COMMAND "${VIPER_EXE}" ${ARGN}
            RESULT_VARIABLE _rv
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE _err)
    if (NOT _rv EQUAL 0)
        message(FATAL_ERROR "${name} returned ${_rv}\nstdout:\n${_out}\nstderr:\n${_err}")
    endif ()
    set(${name}_OUT "${_out}" PARENT_SCOPE)
    set(${name}_ERR "${_err}" PARENT_SCOPE)
    set(${name}_TEXT "${_out}${_err}" PARENT_SCOPE)
endfunction()

function(run_fail_case name)
    execute_process(
            COMMAND "${VIPER_EXE}" ${ARGN}
            RESULT_VARIABLE _rv
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE _err)
    if (_rv EQUAL 0)
        message(FATAL_ERROR "${name} unexpectedly succeeded\nstdout:\n${_out}\nstderr:\n${_err}")
    endif ()
    set(${name}_TEXT "${_out}${_err}" PARENT_SCOPE)
endfunction()

run_help_case(TOP --help)
if (TOP_TEXT MATCHES "Intrinsics:" OR TOP_TEXT MATCHES "FUNCTION must RETURN")
    message(FATAL_ERROR "top-level help still contains BASIC-specific details\n${TOP_TEXT}")
endif ()
if (NOT TOP_TEXT MATCHES "viper help package")
    message(FATAL_ERROR "top-level help does not point to package help\n${TOP_TEXT}")
endif ()

run_help_case(PACKAGE package --help)
if (PACKAGE_TEXT MATCHES "macos-sign-identity" OR PACKAGE_TEXT MATCHES "windows-sign-thumbprint")
    message(FATAL_ERROR "package help still contains platform signing reference details\n${PACKAGE_TEXT}")
endif ()
if (NOT PACKAGE_TEXT MATCHES "docs/tools.md")
    message(FATAL_ERROR "package help should point to detailed docs\n${PACKAGE_TEXT}")
endif ()

run_help_case(BENCH bench --help)
if (NOT BENCH_TEXT MATCHES "Maximum interpreter steps")
    message(FATAL_ERROR "bench help output was not shown\n${BENCH_TEXT}")
endif ()

run_help_case(ILOPT il-opt --help)
if (NOT ILOPT_TEXT MATCHES "viper il-opt")
    message(FATAL_ERROR "il-opt help output was not shown\n${ILOPT_TEXT}")
endif ()

run_help_case(BUILD_MANY build-many --help)
if (NOT BUILD_MANY_TEXT MATCHES "name=project" OR
    NOT BUILD_MANY_TEXT MATCHES "--output-dir")
    message(FATAL_ERROR "build-many help output is incomplete\n${BUILD_MANY_TEXT}")
endif ()

run_help_case(CODEGEN_X64 codegen x64 --help)
if (NOT CODEGEN_X64_TEXT MATCHES "--time-passes" OR
    NOT CODEGEN_X64_TEXT MATCHES "--skip-il-optimization")
    message(FATAL_ERROR "x64 codegen performance flags are missing from help\n${CODEGEN_X64_TEXT}")
endif ()

run_help_case(CODEGEN_ARM64 codegen arm64 --help)
if (NOT CODEGEN_ARM64_TEXT MATCHES "--time-passes" OR
    NOT CODEGEN_ARM64_TEXT MATCHES "--skip-il-optimization")
    message(FATAL_ERROR "arm64 codegen performance flags are missing from help\n${CODEGEN_ARM64_TEXT}")
endif ()

run_fail_case(BENCH_BAD_ITER bench missing.il -n 0)
if (NOT BENCH_BAD_ITER_TEXT MATCHES "invalid iteration count")
    message(FATAL_ERROR "bench did not reject zero iterations\n${BENCH_BAD_ITER_TEXT}")
endif ()

run_fail_case(BENCH_BAD_STEPS bench missing.il --max-steps nope)
if (NOT BENCH_BAD_STEPS_TEXT MATCHES "invalid --max-steps value")
    message(FATAL_ERROR "bench did not reject invalid max steps\n${BENCH_BAD_STEPS_TEXT}")
endif ()

run_fail_case(CODEGEN_X64_BAD_OPT codegen x64 missing.il -O 9)
if (NOT CODEGEN_X64_BAD_OPT_TEXT MATCHES "invalid -O level")
    message(FATAL_ERROR "x64 codegen did not reject invalid optimization level\n${CODEGEN_X64_BAD_OPT_TEXT}")
endif ()

run_fail_case(CODEGEN_ARM64_BAD_STACK codegen arm64 missing.il --stack-size=nope)
if (NOT CODEGEN_ARM64_BAD_STACK_TEXT MATCHES "invalid --stack-size value")
    message(FATAL_ERROR "arm64 codegen did not reject invalid stack size\n${CODEGEN_ARM64_BAD_STACK_TEXT}")
endif ()

run_fail_case(BUILD_MANY_BAD_NAME build-many --output-dir out ../bad=missing)
if (NOT BUILD_MANY_BAD_NAME_TEXT MATCHES "one path component")
    message(FATAL_ERROR "build-many did not reject an unsafe output name\n${BUILD_MANY_BAD_NAME_TEXT}")
endif ()
