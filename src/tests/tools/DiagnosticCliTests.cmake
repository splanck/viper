cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED VIPER_EXE)
    message(FATAL_ERROR "VIPER_EXE must be provided")
endif ()
if (NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR must be provided")
endif ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

set(_unused_zia "${TEST_WORK_DIR}/unused.zia")
file(WRITE "${_unused_zia}" "module T;\nfunc start() {    var x = 5;\n}\n")

execute_process(
        COMMAND "${VIPER_EXE}" front zia -emit-il "${_unused_zia}"
        RESULT_VARIABLE _unused_rc
        OUTPUT_VARIABLE _unused_out
        ERROR_VARIABLE _unused_err)
if (NOT _unused_rc EQUAL 0)
    message(FATAL_ERROR "unused-variable warning case failed unexpectedly:\n${_unused_err}")
endif ()
if (NOT _unused_err MATCHES "warning\\[W001\\]")
    message(FATAL_ERROR "successful Zia compile did not print W001 warning:\n${_unused_err}")
endif ()

execute_process(
        COMMAND "${VIPER_EXE}" front zia -emit-il --diagnostic-format=json "${_unused_zia}"
        RESULT_VARIABLE _json_rc
        OUTPUT_VARIABLE _json_out
        ERROR_VARIABLE _json_err)
if (NOT _json_rc EQUAL 0)
    message(FATAL_ERROR "JSON diagnostic warning case failed unexpectedly:\n${_json_err}")
endif ()
if (NOT _json_err MATCHES "\"diagnostics\"" OR NOT _json_err MATCHES "\"code\":\"W001\"")
    message(FATAL_ERROR "JSON diagnostic output did not include W001:\n${_json_err}")
endif ()

set(_div0_zia "${TEST_WORK_DIR}/div0.zia")
file(WRITE "${_div0_zia}" "module T;\nfunc start() {    var x = 10 / 0;\n}\n")

execute_process(
        COMMAND "${VIPER_EXE}" front zia -emit-il --strict-diagnostics "${_div0_zia}"
        RESULT_VARIABLE _div0_rc
        OUTPUT_VARIABLE _div0_out
        ERROR_VARIABLE _div0_err)
if (_div0_rc EQUAL 0)
    message(FATAL_ERROR "strict diagnostics did not reject literal division by zero")
endif ()
if (NOT _div0_err MATCHES "error\\[W010\\]")
    message(FATAL_ERROR "strict diagnostics did not promote W010 to an error:\n${_div0_err}")
endif ()

execute_process(
        COMMAND "${VIPER_EXE}" front zia -emit-il --no-strict-diagnostics "${_div0_zia}"
        RESULT_VARIABLE _nostrict_rc
        OUTPUT_VARIABLE _nostrict_out
        ERROR_VARIABLE _nostrict_err)
if (NOT _nostrict_rc EQUAL 0)
    message(FATAL_ERROR "--no-strict-diagnostics should allow W010 warning:\n${_nostrict_err}")
endif ()
if (NOT _nostrict_err MATCHES "warning\\[W010\\]")
    message(FATAL_ERROR "--no-strict-diagnostics did not print W010 warning:\n${_nostrict_err}")
endif ()

set(_gaddr_il "${TEST_WORK_DIR}/bytecode_gaddr.il")
file(WRITE "${_gaddr_il}" "il 0.2.0\n"
                          "global const str @gvar = \"test\"\n"
                          "func @get_addr() -> ptr {\n"
                          "entry:\n"
                          "  %p = gaddr @gvar\n"
                          "  ret %p\n"
                          "}\n")

execute_process(
        COMMAND "${VIPER_EXE}" -run "${_gaddr_il}" --bytecode --dump-trap
        RESULT_VARIABLE _gaddr_rc
        OUTPUT_VARIABLE _gaddr_out
        ERROR_VARIABLE _gaddr_err)
if (_gaddr_rc EQUAL 0)
    message(FATAL_ERROR "bytecode gaddr case unexpectedly succeeded")
endif ()
if (NOT _gaddr_err MATCHES "error\\[V-BC-UNSUPPORTED-GADDR\\]")
    message(FATAL_ERROR "bytecode gaddr case did not print structured diagnostic:\n${_gaddr_err}")
endif ()
if (_gaddr_err MATCHES "libc\\+\\+abi|terminate called|Abort trap|SIGABRT")
    message(FATAL_ERROR "bytecode gaddr case appears to have crashed:\n${_gaddr_err}")
endif ()
