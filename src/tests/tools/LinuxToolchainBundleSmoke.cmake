cmake_minimum_required(VERSION 3.20)

#===----------------------------------------------------------------------===//
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//

foreach (_required CMAKE_BIN ZANNA_BIN ZANNA_BUILD_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to LinuxToolchainBundleSmoke.cmake")
    endif ()
endforeach ()

include("${CMAKE_CURRENT_LIST_DIR}/ToolchainInstallerSmokeHelpers.cmake")

if (NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Skipping Linux self-extracting bundle smoke; host is not Linux")
    return()
endif ()

set(_tmp_root "${ZANNA_BUILD_DIR}/tests/linux-toolchain-bundle-smoke")
set(_artifact "${_tmp_root}/zanna-toolchain.run")
set(_stage_dir "${_tmp_root}/stage")
file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}")

set(_install_cmd "${CMAKE_BIN}" --install "${ZANNA_BUILD_DIR}" --prefix "${_stage_dir}")
if (DEFINED ZANNA_CONFIG AND NOT "${ZANNA_CONFIG}" STREQUAL "")
    list(APPEND _install_cmd --config "${ZANNA_CONFIG}")
endif ()
execute_process(
        COMMAND ${_install_cmd}
        RESULT_VARIABLE _stage_rv
        OUTPUT_VARIABLE _stage_out
        ERROR_VARIABLE _stage_err)
if (NOT _stage_rv EQUAL 0)
    message(FATAL_ERROR
            "cmake --install failed while staging Linux bundle smoke payload\nstdout:\n${_stage_out}\nstderr:\n${_stage_err}")
endif ()

zanna_installer_smoke_required_tool_names(_required_tools)
foreach (_tool IN LISTS _required_tools)
    if (NOT EXISTS "${_stage_dir}/bin/${_tool}")
        message(FATAL_ERROR "Linux bundle smoke stage is missing ${_stage_dir}/bin/${_tool}")
    endif ()
endforeach ()

set(_install_package_cmd
        "${ZANNA_BIN}" install-package
        --stage-dir "${_stage_dir}"
        --target linux-bundle
        -o "${_artifact}")

execute_process(
        COMMAND ${_install_package_cmd}
        RESULT_VARIABLE _build_rv
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err
)
if (NOT _build_rv EQUAL 0)
    message(FATAL_ERROR
            "zanna install-package --target linux-bundle failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

if (NOT EXISTS "${_artifact}")
    message(FATAL_ERROR "expected Linux bundle artifact was not created: ${_artifact}")
endif ()

execute_process(
        COMMAND "${ZANNA_BIN}" install-package --verify-only "${_artifact}"
        RESULT_VARIABLE _verify_rv
        OUTPUT_VARIABLE _verify_out
        ERROR_VARIABLE _verify_err
)
if (NOT _verify_rv EQUAL 0)
    message(FATAL_ERROR
            "zanna install-package --verify-only failed for Linux bundle\nstdout:\n${_verify_out}\nstderr:\n${_verify_err}")
endif ()

execute_process(
        COMMAND chmod +x "${_artifact}"
        RESULT_VARIABLE _chmod_rv
        OUTPUT_VARIABLE _chmod_out
        ERROR_VARIABLE _chmod_err
)
if (NOT _chmod_rv EQUAL 0)
    message(FATAL_ERROR "chmod +x failed for Linux bundle\nstdout:\n${_chmod_out}\nstderr:\n${_chmod_err}")
endif ()

execute_process(
        COMMAND "${CMAKE_BIN}" -E env
                "XDG_CACHE_HOME=${_tmp_root}/cache"
                "ZANNA_BUNDLE_QUIET=1"
                "${_artifact}" --version
        RESULT_VARIABLE _run_rv
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err
)
if (NOT _run_rv EQUAL 0)
    message(FATAL_ERROR "Linux bundle --version failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()

# Exercise first-launch extraction contention: all processes must converge on one
# content-addressed cache and leave no extraction lock behind.
file(REMOVE_RECURSE "${_tmp_root}/concurrent-cache")
execute_process(
        COMMAND "${CMAKE_BIN}" -E env
                "ZANNA_BUNDLE=${_artifact}"
                "ZANNA_CACHE=${_tmp_root}/concurrent-cache"
                /bin/sh -c [=[
set -eu
pids=
i=1
while [ "$i" -le 4 ]; do
    XDG_CACHE_HOME="$ZANNA_CACHE" ZANNA_BUNDLE_QUIET=1 "$ZANNA_BUNDLE" --version \
        >"$ZANNA_CACHE.out.$i" 2>"$ZANNA_CACHE.err.$i" &
    pids="$pids $!"
    i=$((i + 1))
done
for pid in $pids; do wait "$pid"; done
]=]
        RESULT_VARIABLE _concurrent_rv
        OUTPUT_VARIABLE _concurrent_out
        ERROR_VARIABLE _concurrent_err)
if (NOT _concurrent_rv EQUAL 0)
    message(FATAL_ERROR
            "concurrent Linux bundle extraction failed\nstdout:\n${_concurrent_out}\nstderr:\n${_concurrent_err}")
endif ()
file(GLOB_RECURSE _cache_stamps "${_tmp_root}/concurrent-cache/zanna/.payload.sha256")
list(LENGTH _cache_stamps _cache_stamp_count)
if (NOT _cache_stamp_count EQUAL 1)
    message(FATAL_ERROR
            "concurrent Linux bundle extraction expected one content-addressed cache stamp, found ${_cache_stamp_count}")
endif ()
file(GLOB _cache_locks "${_tmp_root}/concurrent-cache/zanna/.*.lock")
if (_cache_locks)
    message(FATAL_ERROR "concurrent Linux bundle extraction left lock directories: ${_cache_locks}")
endif ()
