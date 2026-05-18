cmake_minimum_required(VERSION 3.20)

foreach (_required VIPER_BIN VIPER_BUILD_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to LinuxToolchainDebInstallerSmoke.cmake")
    endif ()
endforeach ()

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Skipping Linux installer smoke; host is not Linux")
    return()
endif ()

find_program(DPKG_DEB_BIN dpkg-deb)
if (NOT DPKG_DEB_BIN)
    message(STATUS "Skipping Linux installer smoke; dpkg-deb is not available")
    return()
endif ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/linux-toolchain-deb-installer-smoke")
set(_artifact "${_tmp_root}/viper-toolchain.deb")
file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}")

set(_install_package_cmd
        "${VIPER_BIN}" install-package
        --build-dir "${VIPER_BUILD_DIR}"
        --target linux-deb
        --no-verify
        -o "${_artifact}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    list(APPEND _install_package_cmd --config "${VIPER_CONFIG}")
endif ()

execute_process(
        COMMAND ${_install_package_cmd}
        RESULT_VARIABLE _build_rv
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err
)
if (NOT _build_rv EQUAL 0)
    message(FATAL_ERROR
            "viper install-package --target linux-deb failed\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

if (NOT EXISTS "${_artifact}")
    message(FATAL_ERROR "expected .deb artifact was not created: ${_artifact}")
endif ()

execute_process(
        COMMAND "${VIPER_BIN}" install-package --verify-only "${_artifact}"
        RESULT_VARIABLE _verify_rv
        OUTPUT_VARIABLE _verify_out
        ERROR_VARIABLE _verify_err
)
if (NOT _verify_rv EQUAL 0)
    message(FATAL_ERROR
            "viper install-package --verify-only failed for .deb\nstdout:\n${_verify_out}\nstderr:\n${_verify_err}")
endif ()

execute_process(
        COMMAND "${DPKG_DEB_BIN}" -I "${_artifact}"
        RESULT_VARIABLE _info_rv
        OUTPUT_VARIABLE _info_out
        ERROR_VARIABLE _info_err
)
if (NOT _info_rv EQUAL 0)
    message(FATAL_ERROR "dpkg-deb -I failed\nstdout:\n${_info_out}\nstderr:\n${_info_err}")
endif ()
foreach (_needle IN ITEMS
        "Package: viper"
        "Depends:"
        "Recommends:"
        "Viper compiler toolchain")
    if (NOT _info_out MATCHES "${_needle}")
        message(FATAL_ERROR "dpkg-deb -I output did not contain '${_needle}'\n${_info_out}")
    endif ()
endforeach ()

execute_process(
        COMMAND "${DPKG_DEB_BIN}" -c "${_artifact}"
        RESULT_VARIABLE _list_rv
        OUTPUT_VARIABLE _list_out
        ERROR_VARIABLE _list_err
)
if (NOT _list_rv EQUAL 0)
    message(FATAL_ERROR "dpkg-deb -c failed\nstdout:\n${_list_out}\nstderr:\n${_list_err}")
endif ()
foreach (_path IN ITEMS
        "./usr/bin/viper"
        "./usr/lib/cmake/Viper/ViperConfig.cmake"
        "./usr/share/man/man1/viper.1")
    if (NOT _list_out MATCHES "${_path}")
        message(FATAL_ERROR "dpkg-deb payload listing did not contain '${_path}'\n${_list_out}")
    endif ()
endforeach ()

if (NOT DEFINED ENV{VIPER_RUN_LINUX_INSTALLER_SMOKE} OR NOT "$ENV{VIPER_RUN_LINUX_INSTALLER_SMOKE}" STREQUAL "1")
    message(STATUS "Skipping Linux installer smoke; set VIPER_RUN_LINUX_INSTALLER_SMOKE=1 to install the .deb")
    return()
endif ()

find_program(DPKG_BIN dpkg)
if (NOT DPKG_BIN)
    message(STATUS "Skipping Linux installer smoke; dpkg is not available")
    return()
endif ()

execute_process(
        COMMAND id -u
        RESULT_VARIABLE _id_rv
        OUTPUT_VARIABLE _uid
        ERROR_VARIABLE _id_err
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT _id_rv EQUAL 0 OR NOT _uid STREQUAL "0")
    message(STATUS "Skipping Linux installer smoke; installing the .deb requires root")
    return()
endif ()

execute_process(
        COMMAND "${DPKG_BIN}" -i "${_artifact}"
        RESULT_VARIABLE _install_rv
        OUTPUT_VARIABLE _install_out
        ERROR_VARIABLE _install_err
)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR "dpkg -i failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

execute_process(
        COMMAND /usr/bin/viper --version
        RESULT_VARIABLE _run_rv
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err
)
if (NOT _run_rv EQUAL 0)
    message(FATAL_ERROR "installed /usr/bin/viper --version failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()

execute_process(
        COMMAND "${DPKG_BIN}" -r viper
        RESULT_VARIABLE _remove_rv
        OUTPUT_VARIABLE _remove_out
        ERROR_VARIABLE _remove_err
)
if (NOT _remove_rv EQUAL 0)
    message(FATAL_ERROR "dpkg -r viper failed\nstdout:\n${_remove_out}\nstderr:\n${_remove_err}")
endif ()
