#===----------------------------------------------------------------------===//
#
# Part of the Viper project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//
#
# File: src/tests/e2e/test_windows_native_environment.cmake
# Purpose: Regression probes for Windows native Environment runtime startup.
# Key invariants:
#   - Each generated probe must build as a native executable with viper build.
#   - Each probe runs under a short timeout so argument/env startup hangs fail fast.
# Ownership/Lifetime:
#   - Test sources and executables are generated under TEST_WORK_DIR.
#
#===----------------------------------------------------------------------===//

if (NOT DEFINED VIPER_EXE)
    message(FATAL_ERROR "VIPER_EXE must be provided")
endif ()

if (NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR must be provided")
endif ()

file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

function(write_probe name body)
    set(_path "${TEST_WORK_DIR}/${name}.zia")
    file(WRITE "${_path}" "${body}")
    set(${name}_SRC "${_path}" PARENT_SCOPE)
    set(${name}_EXE "${TEST_WORK_DIR}/${name}.exe" PARENT_SCOPE)
endfunction()

function(build_probe name)
    execute_process(
            COMMAND "${VIPER_EXE}" build "${${name}_SRC}" -o "${${name}_EXE}" --quiet-warnings
            TIMEOUT 30
            RESULT_VARIABLE _build_rc
            OUTPUT_VARIABLE _build_out
            ERROR_VARIABLE _build_err)
    if (NOT _build_rc EQUAL 0)
        message(FATAL_ERROR "${name}: native build failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
    endif ()
endfunction()

function(run_probe name expect_regex)
    execute_process(
            COMMAND "${${name}_EXE}"
            TIMEOUT 5
            RESULT_VARIABLE _run_rc
            OUTPUT_VARIABLE _run_out
            ERROR_VARIABLE _run_err)
    if (NOT _run_rc EQUAL 0)
        message(FATAL_ERROR "${name}: native run failed with ${_run_rc}\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
    endif ()
    if (NOT _run_out MATCHES "${expect_regex}")
        message(FATAL_ERROR "${name}: output mismatch\nexpected regex: ${expect_regex}\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
    endif ()
endfunction()

function(run_probe_exit name expected_code)
    execute_process(
            COMMAND "${${name}_EXE}"
            TIMEOUT 5
            RESULT_VARIABLE _run_rc
            OUTPUT_VARIABLE _run_out
            ERROR_VARIABLE _run_err)
    if (NOT _run_rc EQUAL ${expected_code})
        message(FATAL_ERROR "${name}: expected exit ${expected_code}, got ${_run_rc}\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
    endif ()
endfunction()

write_probe(native_env_arg_count
"module NativeEnvArgCount;
func start() {
    Viper.Terminal.SayInt(Viper.System.Environment.GetArgumentCount());
}
")

write_probe(native_env_arg_get
"module NativeEnvArgGet;
func start() {
    var count = Viper.System.Environment.GetArgumentCount();
    if count > 0 {
        var arg0 = Viper.System.Environment.GetArgument(0);
        if Viper.String.get_Length(arg0) > 0 {
            Viper.Terminal.Say(\"RESULT: ok\");
            return;
        }
    }
    Viper.Terminal.Say(\"RESULT: fail\");
}
")

write_probe(native_env_get_missing
"module NativeEnvGetMissing;
func start() {
    var value = Viper.System.Environment.GetVariable(\"VIPER_NATIVE_ENV_MISSING_SENTINEL\");
    if value == \"\" {
        Viper.Terminal.Say(\"RESULT: ok\");
        return;
    }
    Viper.Terminal.Say(\"RESULT: fail\");
}
")

write_probe(native_env_long_heap_string
"module NativeEnvLongHeapString;
func start() {
    var value = \"abcdefghijklmnopqrstuvwxyz\" + \"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\";
    if Viper.String.get_Length(value) > 40 {
        Viper.Terminal.Say(\"RESULT: ok\");
        return;
    }
    Viper.Terminal.Say(\"RESULT: fail\");
}
")

write_probe(native_env_set_get_long
"module NativeEnvSetGetLong;
func start() {
    var name = \"VIPER_NATIVE_ENV_SET_GET_LONG_SENTINEL\";
    var value = \"native-environment-value-\" + \"abcdefghijklmnopqrstuvwxyz0123456789\";
    Viper.System.Environment.SetVariable(name, value);
    if Viper.System.Environment.HasVariable(name) && Viper.System.Environment.GetVariable(name) == value {
        Viper.Terminal.Say(\"RESULT: ok\");
        return;
    }
    Viper.Terminal.Say(\"RESULT: fail\");
}
")

write_probe(native_env_is_native
"module NativeEnvIsNative;
func start() {
    if Viper.System.Environment.IsNative() {
        Viper.Terminal.Say(\"RESULT: ok\");
        return;
    }
    Viper.Terminal.Say(\"RESULT: fail\");
}
")

write_probe(native_env_end_program
"module NativeEnvEndProgram;
func start() {
    Viper.System.Environment.EndProgram(7);
}
")

foreach (_probe
        native_env_arg_count
        native_env_arg_get
        native_env_get_missing
        native_env_long_heap_string
        native_env_set_get_long
        native_env_is_native
        native_env_end_program)
    build_probe(${_probe})
endforeach ()

run_probe(native_env_arg_count "^[0-9]+")
run_probe(native_env_arg_get "RESULT: ok")
run_probe(native_env_get_missing "RESULT: ok")
run_probe(native_env_long_heap_string "RESULT: ok")
run_probe(native_env_set_get_long "RESULT: ok")
run_probe(native_env_is_native "RESULT: ok")
run_probe_exit(native_env_end_program 7)
