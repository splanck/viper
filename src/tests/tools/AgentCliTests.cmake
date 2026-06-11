# Tests for the agent-facing CLI surface: viper check, viper eval,
# viper explain, --print-error-codes, --dump-runtime-api, --dump-opcodes.
# Validates exit-code contracts and machine-readable output shapes.
cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED VIPER_EXE)
    message(FATAL_ERROR "VIPER_EXE must be provided")
endif ()
if (NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR must be provided")
endif ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

# ===== viper check: clean file exits 0 =====
set(_ok_zia "${TEST_WORK_DIR}/ok.zia")
file(WRITE "${_ok_zia}" "module T;\nbind Viper.Terminal;\nfunc start() {\n    Say(\"hi\");\n}\n")

execute_process(
        COMMAND "${VIPER_EXE}" check "${_ok_zia}"
        RESULT_VARIABLE _check_ok_rc
        OUTPUT_VARIABLE _check_ok_out
        ERROR_VARIABLE _check_ok_err)
if (NOT _check_ok_rc EQUAL 0)
    message(FATAL_ERROR "viper check on a clean file should exit 0, got ${_check_ok_rc}:\n${_check_ok_err}")
endif ()

# ===== viper check: compile errors exit 2 with structured JSON =====
set(_bad_zia "${TEST_WORK_DIR}/bad.zia")
file(WRITE "${_bad_zia}" "module T;\nbind Viper.Terminal;\nfunc start() {\n    var count = 1;\n    SayInt(cout + count);\n}\n")

execute_process(
        COMMAND "${VIPER_EXE}" check "${_bad_zia}" --diagnostic-format=json
        RESULT_VARIABLE _check_bad_rc
        OUTPUT_VARIABLE _check_bad_out
        ERROR_VARIABLE _check_bad_err)
if (NOT _check_bad_rc EQUAL 2)
    message(FATAL_ERROR "viper check on a broken file should exit 2, got ${_check_bad_rc}:\n${_check_bad_err}")
endif ()
if (NOT _check_bad_err MATCHES "\"code\":\"V-ZIA-UNDEFINED\"")
    message(FATAL_ERROR "viper check JSON did not include the V-ZIA-UNDEFINED code:\n${_check_bad_err}")
endif ()
if (NOT _check_bad_err MATCHES "\"fixits\"" OR NOT _check_bad_err MATCHES "\"replacement\":\"count\"")
    message(FATAL_ERROR "viper check JSON did not carry the did-you-mean fix-it:\n${_check_bad_err}")
endif ()

# ===== viper check: unresolvable target exits 1 =====
execute_process(
        COMMAND "${VIPER_EXE}" check "${TEST_WORK_DIR}/does-not-exist.zia"
        RESULT_VARIABLE _check_missing_rc
        OUTPUT_VARIABLE _check_missing_out
        ERROR_VARIABLE _check_missing_err)
if (NOT _check_missing_rc EQUAL 1)
    message(FATAL_ERROR "viper check on a missing target should exit 1, got ${_check_missing_rc}")
endif ()

# ===== viper check: rejects run/build-only flags =====
execute_process(
        COMMAND "${VIPER_EXE}" check "${_ok_zia}" -o out.il
        RESULT_VARIABLE _check_o_rc
        OUTPUT_VARIABLE _check_o_out
        ERROR_VARIABLE _check_o_err)
if (_check_o_rc EQUAL 0)
    message(FATAL_ERROR "viper check should reject -o")
endif ()

# ===== viper eval: expression auto-print, exit 0 =====
execute_process(
        COMMAND "${VIPER_EXE}" eval "2 + 3 * 4"
        RESULT_VARIABLE _eval_rc
        OUTPUT_VARIABLE _eval_out
        ERROR_VARIABLE _eval_err)
if (NOT _eval_rc EQUAL 0)
    message(FATAL_ERROR "viper eval should exit 0, got ${_eval_rc}:\n${_eval_err}")
endif ()
if (NOT _eval_out MATCHES "14")
    message(FATAL_ERROR "viper eval '2 + 3 * 4' should print 14, got:\n${_eval_out}")
endif ()

# ===== viper eval --json --type: structured result =====
execute_process(
        COMMAND "${VIPER_EXE}" eval --json --type "1 + 1"
        RESULT_VARIABLE _eval_json_rc
        OUTPUT_VARIABLE _eval_json_out
        ERROR_VARIABLE _eval_json_err)
if (NOT _eval_json_rc EQUAL 0)
    message(FATAL_ERROR "viper eval --json should exit 0, got ${_eval_json_rc}:\n${_eval_json_err}")
endif ()
if (NOT _eval_json_out MATCHES "\"success\":true" OR NOT _eval_json_out MATCHES "\"resultType\":\"Integer\"")
    message(FATAL_ERROR "viper eval --json output malformed:\n${_eval_json_out}")
endif ()
if (NOT _eval_json_out MATCHES "\"type\":\"Integer\"")
    message(FATAL_ERROR "viper eval --type did not report the inferred type:\n${_eval_json_out}")
endif ()

# ===== viper eval: compile errors exit 2 =====
execute_process(
        COMMAND "${VIPER_EXE}" eval "thisIsNotDefined + 1"
        RESULT_VARIABLE _eval_bad_rc
        OUTPUT_VARIABLE _eval_bad_out
        ERROR_VARIABLE _eval_bad_err)
if (NOT _eval_bad_rc EQUAL 2)
    message(FATAL_ERROR "viper eval on bad code should exit 2, got ${_eval_bad_rc}")
endif ()

# ===== viper eval: runtime traps exit 3 =====
execute_process(
        COMMAND "${VIPER_EXE}" eval "1 / 0"
        RESULT_VARIABLE _eval_trap_rc
        OUTPUT_VARIABLE _eval_trap_out
        ERROR_VARIABLE _eval_trap_err)
if (NOT _eval_trap_rc EQUAL 3)
    message(FATAL_ERROR "viper eval on trapping code should exit 3, got ${_eval_trap_rc}")
endif ()

# ===== viper explain: cataloged code =====
execute_process(
        COMMAND "${VIPER_EXE}" explain V-ZIA-UNDEFINED
        RESULT_VARIABLE _explain_rc
        OUTPUT_VARIABLE _explain_out
        ERROR_VARIABLE _explain_err)
if (NOT _explain_rc EQUAL 0)
    message(FATAL_ERROR "viper explain V-ZIA-UNDEFINED should exit 0, got ${_explain_rc}:\n${_explain_err}")
endif ()
if (NOT _explain_out MATCHES "zia-sema")
    message(FATAL_ERROR "viper explain output missing subsystem:\n${_explain_out}")
endif ()

# ===== viper explain --json =====
execute_process(
        COMMAND "${VIPER_EXE}" explain W001 --json
        RESULT_VARIABLE _explain_json_rc
        OUTPUT_VARIABLE _explain_json_out
        ERROR_VARIABLE _explain_json_err)
if (NOT _explain_json_rc EQUAL 0 OR NOT _explain_json_out MATCHES "\"code\":\"W001\"")
    message(FATAL_ERROR "viper explain W001 --json malformed:\n${_explain_json_out}")
endif ()

# ===== viper explain: uncataloged code with known prefix still resolves =====
execute_process(
        COMMAND "${VIPER_EXE}" explain B2113
        RESULT_VARIABLE _explain_family_rc
        OUTPUT_VARIABLE _explain_family_out
        ERROR_VARIABLE _explain_family_err)
if (NOT _explain_family_rc EQUAL 0)
    message(FATAL_ERROR "viper explain on an uncataloged BASIC code should fall back to its family, got ${_explain_family_rc}")
endif ()

# ===== viper explain: unknown code exits 1 =====
execute_process(
        COMMAND "${VIPER_EXE}" explain TOTALLY-BOGUS
        RESULT_VARIABLE _explain_unknown_rc
        OUTPUT_VARIABLE _explain_unknown_out
        ERROR_VARIABLE _explain_unknown_err)
if (_explain_unknown_rc EQUAL 0)
    message(FATAL_ERROR "viper explain on an unknown code should exit non-zero")
endif ()

# ===== --print-error-codes --json =====
execute_process(
        COMMAND "${VIPER_EXE}" --print-error-codes --json
        RESULT_VARIABLE _codes_rc
        OUTPUT_VARIABLE _codes_out
        ERROR_VARIABLE _codes_err)
if (NOT _codes_rc EQUAL 0)
    message(FATAL_ERROR "--print-error-codes --json should exit 0, got ${_codes_rc}")
endif ()
if (NOT _codes_out MATCHES "\"code\":\"V-ZIA-TYPE-MISMATCH\"" OR NOT _codes_out MATCHES "\"code\":\"B2001\"")
    message(FATAL_ERROR "--print-error-codes catalog is missing expected entries:\n${_codes_out}")
endif ()

# ===== --dump-runtime-api =====
execute_process(
        COMMAND "${VIPER_EXE}" --dump-runtime-api
        RESULT_VARIABLE _api_rc
        OUTPUT_VARIABLE _api_out
        ERROR_VARIABLE _api_err)
if (NOT _api_rc EQUAL 0)
    message(FATAL_ERROR "--dump-runtime-api should exit 0, got ${_api_rc}")
endif ()
if (NOT _api_out MATCHES "\"functions\":" OR NOT _api_out MATCHES "\"classes\":")
    message(FATAL_ERROR "--dump-runtime-api missing top-level sections")
endif ()
if (NOT _api_out MATCHES "Viper.Terminal.Say")
    message(FATAL_ERROR "--dump-runtime-api missing canonical function entries")
endif ()

# ===== --dump-opcodes =====
execute_process(
        COMMAND "${VIPER_EXE}" --dump-opcodes
        RESULT_VARIABLE _ops_rc
        OUTPUT_VARIABLE _ops_out
        ERROR_VARIABLE _ops_err)
if (NOT _ops_rc EQUAL 0)
    message(FATAL_ERROR "--dump-opcodes should exit 0, got ${_ops_rc}")
endif ()
if (NOT _ops_out MATCHES "\"mnemonic\":\"add\"" OR NOT _ops_out MATCHES "\"opcodes\":")
    message(FATAL_ERROR "--dump-opcodes output malformed:\n${_ops_out}")
endif ()

message(STATUS "Agent CLI tests passed")
