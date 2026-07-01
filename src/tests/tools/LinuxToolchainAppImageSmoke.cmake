cmake_minimum_required(VERSION 3.20)

#===----------------------------------------------------------------------===//
#
# Part of the Viper project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//

foreach (_required CMAKE_BIN VIPER_BIN VIPER_BUILD_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to LinuxToolchainAppImageSmoke.cmake")
    endif ()
endforeach ()

include("${CMAKE_CURRENT_LIST_DIR}/ToolchainInstallerSmokeHelpers.cmake")

if (NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Skipping Linux AppImage smoke; host is not Linux")
    return()
endif ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/linux-toolchain-appimage-smoke")
set(_artifact "${_tmp_root}/Viper-toolchain.AppImage")
set(_stage_dir "${_tmp_root}/stage")
file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}")

set(_install_cmd "${CMAKE_BIN}" --install "${VIPER_BUILD_DIR}" --prefix "${_stage_dir}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    list(APPEND _install_cmd --config "${VIPER_CONFIG}")
endif ()
execute_process(
        COMMAND ${_install_cmd}
        RESULT_VARIABLE _stage_rv
        OUTPUT_VARIABLE _stage_out
        ERROR_VARIABLE _stage_err)
if (NOT _stage_rv EQUAL 0)
    message(FATAL_ERROR
            "cmake --install failed while staging AppImage smoke payload\nstdout:\n${_stage_out}\nstderr:\n${_stage_err}")
endif ()

viper_installer_smoke_required_tool_names(_required_tools)
foreach (_tool IN LISTS _required_tools)
    if (NOT EXISTS "${_stage_dir}/bin/${_tool}")
        message(FATAL_ERROR "AppImage smoke stage is missing ${_stage_dir}/bin/${_tool}")
    endif ()
endforeach ()

set(_install_package_cmd
        "${VIPER_BIN}" install-package
        --stage-dir "${_stage_dir}"
        --target appimage
        -o "${_artifact}")

execute_process(
        COMMAND ${_install_package_cmd}
        RESULT_VARIABLE _build_rv
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err
)
if (NOT _build_rv EQUAL 0)
    message(FATAL_ERROR
            "viper install-package --target appimage failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

if (NOT EXISTS "${_artifact}")
    message(FATAL_ERROR "expected AppImage artifact was not created: ${_artifact}")
endif ()

execute_process(
        COMMAND "${VIPER_BIN}" install-package --verify-only "${_artifact}"
        RESULT_VARIABLE _verify_rv
        OUTPUT_VARIABLE _verify_out
        ERROR_VARIABLE _verify_err
)
if (NOT _verify_rv EQUAL 0)
    message(FATAL_ERROR
            "viper install-package --verify-only failed for AppImage\nstdout:\n${_verify_out}\nstderr:\n${_verify_err}")
endif ()

execute_process(
        COMMAND chmod +x "${_artifact}"
        RESULT_VARIABLE _chmod_rv
        OUTPUT_VARIABLE _chmod_out
        ERROR_VARIABLE _chmod_err
)
if (NOT _chmod_rv EQUAL 0)
    message(FATAL_ERROR "chmod +x failed for AppImage\nstdout:\n${_chmod_out}\nstderr:\n${_chmod_err}")
endif ()

execute_process(
        COMMAND "${_artifact}" --version
        RESULT_VARIABLE _run_rv
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err
)
if (NOT _run_rv EQUAL 0)
    message(FATAL_ERROR "AppImage --version failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()
