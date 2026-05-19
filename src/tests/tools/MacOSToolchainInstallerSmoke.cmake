cmake_minimum_required(VERSION 3.20)

foreach (_required CMAKE_BIN VIPER_BIN VIPER_BUILD_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to MacOSToolchainInstallerSmoke.cmake")
    endif ()
endforeach ()

include("${CMAKE_CURRENT_LIST_DIR}/ToolchainInstallerSmokeHelpers.cmake")

if (NOT "$ENV{VIPER_RUN_MACOS_INSTALLER_SMOKE}" STREQUAL "1")
    message(STATUS "Skipping macOS installer smoke; set VIPER_RUN_MACOS_INSTALLER_SMOKE=1 to install into /usr/local")
    return()
endif ()

execute_process(
        COMMAND id -u
        RESULT_VARIABLE _id_rv
        OUTPUT_VARIABLE _uid
        ERROR_VARIABLE _id_err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT _id_rv EQUAL 0)
    message(FATAL_ERROR "cannot determine uid for macOS installer smoke\nstderr:\n${_id_err}")
endif ()
if (NOT _uid STREQUAL "0")
    message(STATUS "Skipping macOS installer smoke; run CTest as root to install into /usr/local")
    return()
endif ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/macos-toolchain-installer-smoke")
set(_pkg "${_tmp_root}/viper-toolchain.pkg")
set(_src_dir "${_tmp_root}/consumer-src")
set(_build_dir "${_tmp_root}/consumer-build")

file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}" "${_src_dir}")

set(_pkg_cmd
        "${VIPER_BIN}" install-package
        --build-dir "${VIPER_BUILD_DIR}"
        --target macos
        -o "${_pkg}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    list(APPEND _pkg_cmd --config "${VIPER_CONFIG}")
endif ()

execute_process(
        COMMAND ${_pkg_cmd}
        RESULT_VARIABLE _pkg_rv
        OUTPUT_VARIABLE _pkg_out
        ERROR_VARIABLE _pkg_err)
if (NOT _pkg_rv EQUAL 0)
    message(FATAL_ERROR "macOS install-package smoke failed\nstdout:\n${_pkg_out}\nstderr:\n${_pkg_err}")
endif ()

execute_process(
        COMMAND installer -pkg "${_pkg}" -target /
        RESULT_VARIABLE _install_rv
        OUTPUT_VARIABLE _install_out
        ERROR_VARIABLE _install_err)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR "macOS installer failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

execute_process(
        COMMAND /usr/local/bin/viper --version
        RESULT_VARIABLE _version_rv
        OUTPUT_VARIABLE _version_out
        ERROR_VARIABLE _version_err)
if (NOT _version_rv EQUAL 0)
    message(FATAL_ERROR "installed viper --version failed\nstdout:\n${_version_out}\nstderr:\n${_version_err}")
endif ()

viper_installer_smoke_verify_cmake_consumer(
        "${CMAKE_BIN}"
        "${_src_dir}"
        "${_build_dir}"
        "${VIPER_CONFIG}"
        "macOS installer smoke")
viper_installer_smoke_verify_native_codegen(
        "${CMAKE_BIN}"
        /usr/local/bin/viper
        "${_tmp_root}"
        "macOS installer smoke")
