cmake_minimum_required(VERSION 3.20)

foreach (_required VIPER_BIN TEST_WORK_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to PackageCliTests.cmake")
    endif ()
endforeach ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

execute_process(
        COMMAND "${VIPER_BIN}" package --help
        RESULT_VARIABLE _help_rv
        OUTPUT_VARIABLE _help_out
        ERROR_VARIABLE _help_err)
if (NOT _help_rv EQUAL 0)
    message(FATAL_ERROR "viper package --help should exit 0\nstdout:\n${_help_out}\nstderr:\n${_help_err}")
endif ()

execute_process(
        COMMAND "${VIPER_BIN}" package --target
        RESULT_VARIABLE _missing_target_rv
        OUTPUT_VARIABLE _missing_target_out
        ERROR_VARIABLE _missing_target_err)
if (_missing_target_rv EQUAL 0)
    message(FATAL_ERROR "viper package --target without a value should fail")
endif ()

set(_quoted_project "${TEST_WORK_DIR}/quoted-project")
file(MAKE_DIRECTORY "${_quoted_project}/asset dir")
file(MAKE_DIRECTORY "${_quoted_project}/scripts")
file(WRITE "${_quoted_project}/main.zia" "func start() {}\n")
file(WRITE "${_quoted_project}/asset dir/config.txt" "ok\n")
file(WRITE "${_quoted_project}/scripts/post install.sh" "echo post-install\n")
file(WRITE "${_quoted_project}/viper.project"
"project quotedpkg
version 1.0.0
lang zia
entry main.zia
package-name \"Quoted Package\"
asset \"asset dir\" \"data files\"
post-install \"scripts/post install.sh\"
")

execute_process(
        COMMAND "${VIPER_BIN}" package "${_quoted_project}" --target tarball --dry-run
        RESULT_VARIABLE _quoted_rv
        OUTPUT_VARIABLE _quoted_out
        ERROR_VARIABLE _quoted_err)
if (NOT _quoted_rv EQUAL 0)
    message(FATAL_ERROR "quoted package dry-run should succeed\nstdout:\n${_quoted_out}\nstderr:\n${_quoted_err}")
endif ()

set(_missing_project "${TEST_WORK_DIR}/missing-project")
file(MAKE_DIRECTORY "${_missing_project}")
file(WRITE "${_missing_project}/main.zia" "func start() {}\n")
file(WRITE "${_missing_project}/viper.project"
"project missingpkg
version 1.0.0
lang zia
entry main.zia
asset missing data
")

execute_process(
        COMMAND "${VIPER_BIN}" package "${_missing_project}" --target tarball --dry-run
        RESULT_VARIABLE _missing_asset_rv
        OUTPUT_VARIABLE _missing_asset_out
        ERROR_VARIABLE _missing_asset_err)
if (_missing_asset_rv EQUAL 0)
    message(FATAL_ERROR "dry-run with missing asset should fail")
endif ()
if (NOT _missing_asset_err MATCHES "asset source path not found")
    message(FATAL_ERROR "missing asset diagnostic did not mention the missing source\nstdout:\n${_missing_asset_out}\nstderr:\n${_missing_asset_err}")
endif ()

set(_bad_url_project "${TEST_WORK_DIR}/bad-url-project")
file(MAKE_DIRECTORY "${_bad_url_project}")
file(WRITE "${_bad_url_project}/main.zia" "func start() {}\n")
file(WRITE "${_bad_url_project}/viper.project"
"project badurl
version 1.0.0
lang zia
entry main.zia
package-homepage https:///missing-host
")

execute_process(
        COMMAND "${VIPER_BIN}" package "${_bad_url_project}" --target tarball --dry-run
        RESULT_VARIABLE _bad_url_rv
        OUTPUT_VARIABLE _bad_url_out
        ERROR_VARIABLE _bad_url_err)
if (_bad_url_rv EQUAL 0)
    message(FATAL_ERROR "dry-run with malformed package-homepage should fail")
endif ()
if (NOT _bad_url_err MATCHES "URL host")
    message(FATAL_ERROR "bad URL diagnostic did not mention the URL host\nstdout:\n${_bad_url_out}\nstderr:\n${_bad_url_err}")
endif ()

set(_bad_assoc_project "${TEST_WORK_DIR}/bad-assoc-project")
file(MAKE_DIRECTORY "${_bad_assoc_project}")
file(WRITE "${_bad_assoc_project}/main.zia" "func start() {}\n")
file(WRITE "${_bad_assoc_project}/viper.project"
"project badassoc
version 1.0.0
lang zia
entry main.zia
file-assoc . \"Bad\" text/plain
")

execute_process(
        COMMAND "${VIPER_BIN}" package "${_bad_assoc_project}" --target linux --dry-run
        RESULT_VARIABLE _bad_assoc_rv
        OUTPUT_VARIABLE _bad_assoc_out
        ERROR_VARIABLE _bad_assoc_err)
if (_bad_assoc_rv EQUAL 0)
    message(FATAL_ERROR "dry-run with a bare-dot file association should fail")
endif ()
if (NOT _bad_assoc_err MATCHES "dotted extension")
    message(FATAL_ERROR "bad association diagnostic did not mention dotted extension\nstdout:\n${_bad_assoc_out}\nstderr:\n${_bad_assoc_err}")
endif ()

set(_bad_scalar_project "${TEST_WORK_DIR}/bad-scalar-project")
file(MAKE_DIRECTORY "${_bad_scalar_project}")
file(WRITE "${_bad_scalar_project}/main.zia" "func start() {}\n")
file(WRITE "${_bad_scalar_project}/viper.project"
"project badscalar
version 1.0.0
lang zia
entry main.zia
package-name \"Foo\" bar
")

execute_process(
        COMMAND "${VIPER_BIN}" package "${_bad_scalar_project}" --target tarball --dry-run
        RESULT_VARIABLE _bad_scalar_rv
        OUTPUT_VARIABLE _bad_scalar_out
        ERROR_VARIABLE _bad_scalar_err)
if (_bad_scalar_rv EQUAL 0)
    message(FATAL_ERROR "package-name with multiple scalar tokens should fail")
endif ()
if (NOT _bad_scalar_err MATCHES "requires exactly one scalar value")
    message(FATAL_ERROR "bad scalar diagnostic did not mention scalar arity\nstdout:\n${_bad_scalar_out}\nstderr:\n${_bad_scalar_err}")
endif ()

set(_linux_dep_project "${TEST_WORK_DIR}/linux-dep-project")
file(MAKE_DIRECTORY "${_linux_dep_project}")
file(WRITE "${_linux_dep_project}/main.zia" "func start() {}\n")
file(WRITE "${_linux_dep_project}/viper.project"
"project linuxdeps
version 1.0.0
lang zia
entry main.zia
package-category Utility
package-depends libc6 (>= 2.34),\tlibstdc++6 | libc++1
")

execute_process(
        COMMAND "${VIPER_BIN}" package "${_linux_dep_project}" --target linux --dry-run
        RESULT_VARIABLE _linux_dep_rv
        OUTPUT_VARIABLE _linux_dep_out
        ERROR_VARIABLE _linux_dep_err)
if (NOT _linux_dep_rv EQUAL 0)
    message(FATAL_ERROR "valid Debian dependency dry-run should succeed\nstdout:\n${_linux_dep_out}\nstderr:\n${_linux_dep_err}")
endif ()
