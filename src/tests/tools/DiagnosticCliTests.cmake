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
        COMMAND "${VIPER_EXE}" front zia -emit-il --quiet-warnings "${_unused_zia}"
        RESULT_VARIABLE _quiet_rc
        OUTPUT_VARIABLE _quiet_out
        ERROR_VARIABLE _quiet_err)
if (NOT _quiet_rc EQUAL 0)
    message(FATAL_ERROR "quiet-warning Zia compile failed unexpectedly:\n${_quiet_err}")
endif ()
if (_quiet_err MATCHES "warning\\[W001\\]")
    message(FATAL_ERROR "--quiet-warnings still printed W001:\n${_quiet_err}")
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
        COMMAND "${VIPER_EXE}" front zia -emit-il "${_div0_zia}"
        RESULT_VARIABLE _div0_default_rc
        OUTPUT_VARIABLE _div0_default_out
        ERROR_VARIABLE _div0_default_err)
if (_div0_default_rc EQUAL 0)
    message(FATAL_ERROR "default diagnostics did not reject literal division by zero")
endif ()
if (NOT _div0_default_err MATCHES "error\\[W010\\]")
    message(FATAL_ERROR "default diagnostics did not promote W010 to an error:\n${_div0_default_err}")
endif ()

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

set(_bad_comment_zia "${TEST_WORK_DIR}/bad_comment.zia")
file(WRITE "${_bad_comment_zia}" "module T;\n/* unterminated\nfunc start() {}\n")

execute_process(
        COMMAND "${VIPER_EXE}" front zia -emit-il "${_bad_comment_zia}"
        RESULT_VARIABLE _bad_comment_rc
        OUTPUT_VARIABLE _bad_comment_out
        ERROR_VARIABLE _bad_comment_err)
if (_bad_comment_rc EQUAL 0)
    message(FATAL_ERROR "unterminated block comment unexpectedly compiled")
endif ()
if (NOT _bad_comment_err MATCHES "error\\[V-ZIA-LEX-UNTERMINATED-COMMENT\\]")
    message(FATAL_ERROR "unterminated block comment did not produce the specific lexer diagnostic:\n${_bad_comment_err}")
endif ()
if (_bad_comment_out MATCHES "func @")
    message(FATAL_ERROR "lexer-error source still emitted IL:\n${_bad_comment_out}")
endif ()

set(_fixed_oob_zia "${TEST_WORK_DIR}/fixed_oob.zia")
file(WRITE "${_fixed_oob_zia}" "module T;\n"
        "class Numbers {\n"
        "  expose Integer[2] values;\n"
        "  expose func bad() -> Integer { return values[2]; }\n"
        "}\n"
        "func start() { var n = new Numbers(); n.bad(); }\n")

execute_process(
        COMMAND "${VIPER_EXE}" front zia -emit-il "${_fixed_oob_zia}"
        RESULT_VARIABLE _fixed_oob_rc
        OUTPUT_VARIABLE _fixed_oob_out
        ERROR_VARIABLE _fixed_oob_err)
if (_fixed_oob_rc EQUAL 0)
    message(FATAL_ERROR "fixed-array literal out-of-bounds access unexpectedly compiled")
endif ()
if (NOT _fixed_oob_err MATCHES "error\\[V-ZIA-BOUNDS\\]" OR
        NOT _fixed_oob_err MATCHES "fixed array index 2 is out of bounds")
    message(FATAL_ERROR "fixed-array out-of-bounds diagnostic was not specific:\n${_fixed_oob_err}")
endif ()

set(_gaddr_il "${TEST_WORK_DIR}/bytecode_gaddr_unknown_global.il")
file(WRITE "${_gaddr_il}" "il 0.2.0\n"
        "func @main() -> ptr {\n"
        "entry:\n"
        "  %p = gaddr @missing\n"
        "  ret %p\n"
        "}\n")

execute_process(
        COMMAND "${VIPER_EXE}" -run "${_gaddr_il}" --bytecode --dump-trap
        RESULT_VARIABLE _gaddr_rc
        OUTPUT_VARIABLE _gaddr_out
        ERROR_VARIABLE _gaddr_err)
if (_gaddr_rc EQUAL 0)
    message(FATAL_ERROR "bytecode gaddr unknown-global case unexpectedly succeeded")
endif ()
if (NOT _gaddr_err MATCHES "error\\[V-IL-VERIFY\\]" OR
        NOT _gaddr_err MATCHES "unknown global")
    message(FATAL_ERROR "bytecode gaddr unknown-global case did not print structured diagnostic:\n${_gaddr_err}")
endif ()
if (_gaddr_err MATCHES "libc\\+\\+abi|terminate called|Abort trap|SIGABRT")
    message(FATAL_ERROR "bytecode gaddr unknown-global case appears to have crashed:\n${_gaddr_err}")
endif ()
