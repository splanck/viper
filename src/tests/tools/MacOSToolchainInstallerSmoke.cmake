cmake_minimum_required(VERSION 3.20)

foreach (_required CMAKE_BIN ZANNA_BIN ZANNA_BUILD_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to MacOSToolchainInstallerSmoke.cmake")
    endif ()
endforeach ()

include("${CMAKE_CURRENT_LIST_DIR}/ToolchainInstallerSmokeHelpers.cmake")

set(_tmp_root "${ZANNA_BUILD_DIR}/tests/macos-toolchain-installer-smoke")
set(_pkg "${_tmp_root}/zanna-toolchain.pkg")
set(_src_dir "${_tmp_root}/consumer-src")
set(_build_dir "${_tmp_root}/consumer-build")
set(_baseline_pkg "")
set(_upgrade_stale "/usr/local/zanna/share/zanna/installer-upgrade-stale.txt")
set(_upgrade_unrelated "/usr/local/zanna/share/zanna/installer-upgrade-unrelated.txt")
set(_upgrade_unrelated_expected FALSE)

if (DEFINED ZANNA_BASELINE_PACKAGE AND NOT "${ZANNA_BASELINE_PACKAGE}" STREQUAL "")
    if (NOT EXISTS "${ZANNA_BASELINE_PACKAGE}")
        message(FATAL_ERROR "ZANNA_BASELINE_PACKAGE does not exist: ${ZANNA_BASELINE_PACKAGE}")
    endif ()
    get_filename_component(_baseline_pkg "${ZANNA_BASELINE_PACKAGE}" ABSOLUTE)
endif ()

file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}" "${_src_dir}")

if (DEFINED ZANNA_EXISTING_PACKAGE AND NOT "${ZANNA_EXISTING_PACKAGE}" STREQUAL "")
    if (NOT EXISTS "${ZANNA_EXISTING_PACKAGE}")
        message(FATAL_ERROR "ZANNA_EXISTING_PACKAGE does not exist: ${ZANNA_EXISTING_PACKAGE}")
    endif ()
    get_filename_component(_pkg "${ZANNA_EXISTING_PACKAGE}" ABSOLUTE)
else ()
    set(_pkg_cmd
            "${ZANNA_BIN}" install-package
            --build-dir "${ZANNA_BUILD_DIR}"
            --target macos
            -o "${_pkg}")
    if (DEFINED ZANNA_CONFIG AND NOT "${ZANNA_CONFIG}" STREQUAL "")
        list(APPEND _pkg_cmd --config "${ZANNA_CONFIG}")
    endif ()

    execute_process(
            COMMAND ${_pkg_cmd}
            RESULT_VARIABLE _pkg_rv
            OUTPUT_VARIABLE _pkg_out
            ERROR_VARIABLE _pkg_err)
    if (NOT _pkg_rv EQUAL 0)
        message(FATAL_ERROR "macOS install-package smoke failed\nstdout:\n${_pkg_out}\nstderr:\n${_pkg_err}")
    endif ()
endif ()

if ("$ENV{ZANNA_REQUIRE_MACOS_PACKAGE_TRUST}" STREQUAL "1")
    foreach (_trust_command IN ITEMS pkgutil spctl xcrun)
        find_program(_trust_program_${_trust_command} ${_trust_command})
        if (NOT _trust_program_${_trust_command})
            message(FATAL_ERROR "trusted macOS installer smoke requires ${_trust_command}")
        endif ()
    endforeach ()
    execute_process(
            COMMAND pkgutil --check-signature "${_pkg}"
            RESULT_VARIABLE _signature_rv
            OUTPUT_VARIABLE _signature_out
            ERROR_VARIABLE _signature_err)
    if (NOT _signature_rv EQUAL 0)
        message(FATAL_ERROR
                "pkgutil signature verification failed\nstdout:\n${_signature_out}\nstderr:\n${_signature_err}")
    endif ()
    execute_process(
            COMMAND spctl --assess --verbose=2 --type install "${_pkg}"
            RESULT_VARIABLE _gatekeeper_rv
            OUTPUT_VARIABLE _gatekeeper_out
            ERROR_VARIABLE _gatekeeper_err)
    if (NOT _gatekeeper_rv EQUAL 0)
        message(FATAL_ERROR
                "Gatekeeper rejected the package\nstdout:\n${_gatekeeper_out}\nstderr:\n${_gatekeeper_err}")
    endif ()
    execute_process(
            COMMAND xcrun stapler validate "${_pkg}"
            RESULT_VARIABLE _stapler_rv
            OUTPUT_VARIABLE _stapler_out
            ERROR_VARIABLE _stapler_err)
    if (NOT _stapler_rv EQUAL 0)
        message(FATAL_ERROR
                "stapler validation failed\nstdout:\n${_stapler_out}\nstderr:\n${_stapler_err}")
    endif ()
endif ()

execute_process(
        COMMAND installer -pkg "${_pkg}" -target / -showChoicesXML
        RESULT_VARIABLE _choices_rv
        OUTPUT_VARIABLE _choices_out
        ERROR_VARIABLE _choices_err)
if (NOT _choices_rv EQUAL 0 OR NOT _choices_out MATCHES "org[.]zanna[.]toolchain")
    message(FATAL_ERROR
            "macOS Installer.app could not evaluate the generated Distribution choices\nstdout:\n${_choices_out}\nstderr:\n${_choices_err}")
endif ()

if (NOT "$ENV{ZANNA_RUN_MACOS_INSTALLER_SMOKE}" STREQUAL "1")
    message(STATUS "Skipping macOS installer smoke install step; set ZANNA_RUN_MACOS_INSTALLER_SMOKE=1 to install into /usr/local")
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
    message(STATUS "Skipping macOS installer smoke install step; run CTest as root to install into /usr/local")
    return()
endif ()

if (EXISTS "/usr/local/zanna" OR EXISTS "/Applications/Zanna Toolchain.app")
    message(FATAL_ERROR
            "macOS installer lifecycle smoke requires a clean host; remove the existing Zanna Toolchain installation first")
endif ()

if (NOT "${_baseline_pkg}" STREQUAL "")
    execute_process(
            COMMAND installer -pkg "${_baseline_pkg}" -target /
            RESULT_VARIABLE _baseline_install_rv
            OUTPUT_VARIABLE _baseline_install_out
            ERROR_VARIABLE _baseline_install_err)
    if (NOT _baseline_install_rv EQUAL 0)
        message(FATAL_ERROR
                "macOS baseline installer failed\nstdout:\n${_baseline_install_out}\nstderr:\n${_baseline_install_err}")
    endif ()
    if (NOT EXISTS "${_upgrade_stale}")
        message(FATAL_ERROR
                "macOS baseline installer did not install its upgrade-stale file: ${_upgrade_stale}")
    endif ()
    file(WRITE "${_upgrade_unrelated}" "preserve-unowned-upgrade-content\n")
    set(_upgrade_unrelated_expected TRUE)
endif ()

execute_process(
        COMMAND installer -pkg "${_pkg}" -target /
        RESULT_VARIABLE _install_rv
        OUTPUT_VARIABLE _install_out
        ERROR_VARIABLE _install_err)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR "macOS installer failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

if (NOT "${_baseline_pkg}" STREQUAL "")
    if (EXISTS "${_upgrade_stale}" OR IS_SYMLINK "${_upgrade_stale}")
        message(FATAL_ERROR "macOS package upgrade left a stale owned file: ${_upgrade_stale}")
    endif ()
    file(READ "${_upgrade_unrelated}" _upgrade_unrelated_contents)
    string(STRIP "${_upgrade_unrelated_contents}" _upgrade_unrelated_contents)
    if (NOT _upgrade_unrelated_contents STREQUAL "preserve-unowned-upgrade-content")
        message(FATAL_ERROR
                "macOS package upgrade modified unrelated content: ${_upgrade_unrelated}")
    endif ()
endif ()

zanna_installer_smoke_verify_installed_tools("/usr/local/bin" "" "macOS installer smoke")

zanna_installer_smoke_verify_cmake_consumer(
        "${CMAKE_BIN}"
        "${_src_dir}"
        "${_build_dir}"
        "${ZANNA_CONFIG}"
        "macOS installer smoke")
zanna_installer_smoke_verify_native_codegen(
        "${CMAKE_BIN}"
        /usr/local/bin/zanna
        "${_tmp_root}"
        "macOS installer smoke")

set(_uninstaller "/usr/local/zanna/share/zanna/uninstall.sh")
if (NOT EXISTS "${_uninstaller}")
    message(FATAL_ERROR "macOS installer smoke did not install its uninstall helper: ${_uninstaller}")
endif ()
execute_process(
        COMMAND "${_uninstaller}"
        RESULT_VARIABLE _uninstall_rv
        OUTPUT_VARIABLE _uninstall_out
        ERROR_VARIABLE _uninstall_err)
if (NOT _uninstall_rv EQUAL 0)
    message(FATAL_ERROR
            "macOS uninstaller failed\nstdout:\n${_uninstall_out}\nstderr:\n${_uninstall_err}")
endif ()

zanna_installer_smoke_required_tool_names(_uninstalled_tools)
foreach (_tool IN LISTS _uninstalled_tools)
    if (EXISTS "/usr/local/bin/${_tool}" OR IS_SYMLINK "/usr/local/bin/${_tool}")
        message(FATAL_ERROR "macOS uninstaller left an owned command link: /usr/local/bin/${_tool}")
    endif ()
endforeach ()
foreach (_removed_path IN ITEMS
        "/usr/local/lib/cmake/Zanna/ZannaConfig.cmake"
        "/usr/local/lib/cmake/Zanna/ZannaConfigVersion.cmake"
        "/Applications/Zanna Toolchain.app")
    if (EXISTS "${_removed_path}" OR IS_SYMLINK "${_removed_path}")
        message(FATAL_ERROR "macOS uninstaller left an owned path: ${_removed_path}")
    endif ()
endforeach ()

if (_upgrade_unrelated_expected)
    if (NOT EXISTS "${_upgrade_unrelated}" OR IS_SYMLINK "${_upgrade_unrelated}")
        message(FATAL_ERROR
                "macOS uninstaller removed unrelated upgrade-test content: ${_upgrade_unrelated}")
    endif ()
    file(READ "${_upgrade_unrelated}" _uninstall_unrelated_contents)
    string(STRIP "${_uninstall_unrelated_contents}" _uninstall_unrelated_contents)
    if (NOT _uninstall_unrelated_contents STREQUAL "preserve-unowned-upgrade-content")
        message(FATAL_ERROR
                "macOS uninstaller modified unrelated upgrade-test content: ${_upgrade_unrelated}")
    endif ()
    file(REMOVE "${_upgrade_unrelated}")
    execute_process(
            COMMAND rmdir
                    "/usr/local/zanna/share/zanna"
                    "/usr/local/zanna/share"
                    "/usr/local/zanna"
            RESULT_VARIABLE _cleanup_rv
            ERROR_VARIABLE _cleanup_err)
    if (NOT _cleanup_rv EQUAL 0)
        message(FATAL_ERROR
                "macOS lifecycle cleanup found unexpected files under /usr/local/zanna\nstderr:\n${_cleanup_err}")
    endif ()
endif ()
if (EXISTS "/usr/local/zanna" OR IS_SYMLINK "/usr/local/zanna")
    message(FATAL_ERROR "macOS uninstaller left unexpected content under /usr/local/zanna")
endif ()

execute_process(
        COMMAND pkgutil --pkg-info org.zanna.toolchain
        RESULT_VARIABLE _receipt_rv
        OUTPUT_QUIET
        ERROR_QUIET)
if (_receipt_rv EQUAL 0)
    message(FATAL_ERROR "macOS uninstaller left the org.zanna.toolchain package receipt")
endif ()
