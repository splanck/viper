cmake_minimum_required(VERSION 3.20)

foreach (_required VIPER_BIN TEST_WORK_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to WindowsInstallerSmoke.cmake")
    endif ()
endforeach ()

if (NOT WIN32)
    message(STATUS "Skipping Windows installer smoke on non-Windows host")
    return()
endif ()

if (NOT "$ENV{VIPER_RUN_WINDOWS_INSTALLER_SMOKE}" STREQUAL "1")
    message(STATUS "Skipping elevated Windows installer smoke; set VIPER_RUN_WINDOWS_INSTALLER_SMOKE=1 to enable")
    return()
endif ()

execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "$p=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent(); if ($p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { exit 0 } else { exit 1 }"
        RESULT_VARIABLE _admin_rv)
if (NOT _admin_rv EQUAL 0)
    message(STATUS "Skipping Windows installer smoke because the test process is not elevated")
    return()
endif ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}/project")
file(WRITE "${TEST_WORK_DIR}/project/main.zia" "module VAPSSmoke;\n\nfunc start() {}\n")
file(WRITE "${TEST_WORK_DIR}/project/viper.project"
"project vaps_smoke
version 1.0.0
lang zia
entry main.zia
package-name \"VAPS Smoke\"
package-author \"Viper Project\"
package-homepage https://example.invalid/vaps-smoke
package-identifier org.viper.smoke.install
windows-install-scope machine
shortcut-menu on
shortcut-desktop off
file-assoc .vapsmoke \"VAPS Smoke Source\" text/x-vaps-smoke
")

set(_installer "${TEST_WORK_DIR}/vaps-smoke-setup.exe")
execute_process(
        COMMAND "${VIPER_BIN}" package "${TEST_WORK_DIR}/project" --target windows -o "${_installer}" --verbose
        RESULT_VARIABLE _package_rv
        OUTPUT_VARIABLE _package_out
        ERROR_VARIABLE _package_err)
if (NOT _package_rv EQUAL 0)
    message(FATAL_ERROR "Windows package build failed\nstdout:\n${_package_out}\nstderr:\n${_package_err}")
endif ()

execute_process(
        COMMAND "${_installer}" /quiet /norestart
        RESULT_VARIABLE _install_rv
        OUTPUT_VARIABLE _install_out
        ERROR_VARIABLE _install_err
        TIMEOUT 120)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR "Windows installer failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

set(_install_root "$ENV{ProgramFiles}/VAPS Smoke")
set(_app_exe "${_install_root}/vaps_smoke.exe")
set(_uninstall_exe "${_install_root}/uninstall.exe")
if (NOT EXISTS "${_app_exe}")
    message(FATAL_ERROR "Installed app executable is missing: ${_app_exe}")
endif ()
if (NOT EXISTS "${_uninstall_exe}")
    message(FATAL_ERROR "Installed uninstaller is missing: ${_uninstall_exe}")
endif ()

execute_process(
        COMMAND reg query "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.viper.smoke.install"
        RESULT_VARIABLE _reg_rv
        OUTPUT_VARIABLE _reg_out
        ERROR_VARIABLE _reg_err)
if (NOT _reg_rv EQUAL 0)
    message(FATAL_ERROR "Uninstall registry key missing\nstdout:\n${_reg_out}\nstderr:\n${_reg_err}")
endif ()

foreach (_arp_value QuietUninstallString DisplayIcon EstimatedSize InstallDate URLInfoAbout)
    execute_process(
            COMMAND reg query "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.viper.smoke.install" /v "${_arp_value}"
            RESULT_VARIABLE _arp_rv
            OUTPUT_VARIABLE _arp_out
            ERROR_VARIABLE _arp_err)
    if (NOT _arp_rv EQUAL 0)
        message(FATAL_ERROR "Uninstall registry value ${_arp_value} missing\nstdout:\n${_arp_out}\nstderr:\n${_arp_err}")
    endif ()
endforeach ()

execute_process(
        COMMAND reg query "HKLM\\Software\\Classes\\.vapsmoke\\OpenWithProgids"
        RESULT_VARIABLE _assoc_rv
        OUTPUT_VARIABLE _assoc_out
        ERROR_VARIABLE _assoc_err)
if (NOT _assoc_rv EQUAL 0)
    message(FATAL_ERROR "File association key missing\nstdout:\n${_assoc_out}\nstderr:\n${_assoc_err}")
endif ()

execute_process(
        COMMAND "${_uninstall_exe}" /quiet /norestart
        RESULT_VARIABLE _uninstall_rv
        OUTPUT_VARIABLE _uninstall_out
        ERROR_VARIABLE _uninstall_err
        TIMEOUT 120)
if (NOT _uninstall_rv EQUAL 0)
    message(FATAL_ERROR "Windows uninstaller failed\nstdout:\n${_uninstall_out}\nstderr:\n${_uninstall_err}")
endif ()

if (EXISTS "${_app_exe}")
    message(FATAL_ERROR "Installed app executable remained after uninstall: ${_app_exe}")
endif ()

execute_process(
        COMMAND reg query "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.viper.smoke.install"
        RESULT_VARIABLE _reg_after_rv
        OUTPUT_VARIABLE _reg_after_out
        ERROR_VARIABLE _reg_after_err)
if (_reg_after_rv EQUAL 0)
    message(FATAL_ERROR "Uninstall registry key remained after uninstall\nstdout:\n${_reg_after_out}\nstderr:\n${_reg_after_err}")
endif ()
