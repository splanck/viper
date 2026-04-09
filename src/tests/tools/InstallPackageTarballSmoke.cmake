cmake_minimum_required(VERSION 3.20)

foreach (_required VIPER_BIN VIPER_BUILD_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to InstallPackageTarballSmoke.cmake")
    endif ()
endforeach ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/install-package-tarball-smoke")
set(_artifact "${_tmp_root}/viper-toolchain.tar.gz")

file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}")

execute_process(
    COMMAND "${VIPER_BIN}" install-package --build-dir "${VIPER_BUILD_DIR}" --target tarball -o "${_artifact}"
    RESULT_VARIABLE _build_rv
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err
)
if (NOT _build_rv EQUAL 0)
    message(FATAL_ERROR
        "viper install-package tarball smoke failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

if (NOT EXISTS "${_artifact}")
    message(FATAL_ERROR "expected artifact was not created: ${_artifact}")
endif ()

execute_process(
    COMMAND "${VIPER_BIN}" install-package --verify-only "${_artifact}"
    RESULT_VARIABLE _verify_rv
    OUTPUT_VARIABLE _verify_out
    ERROR_VARIABLE _verify_err
)
if (NOT _verify_rv EQUAL 0)
    message(FATAL_ERROR
        "viper install-package verify-only failed\nstdout:\n${_verify_out}\nstderr:\n${_verify_err}")
endif ()
