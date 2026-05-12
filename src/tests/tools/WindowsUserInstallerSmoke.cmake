cmake_minimum_required(VERSION 3.20)

foreach (_required VIPER_BIN TEST_WORK_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to WindowsUserInstallerSmoke.cmake")
    endif ()
endforeach ()

if (NOT WIN32)
    message(STATUS "Skipping Windows user installer smoke on non-Windows host")
    return()
endif ()

if ("$ENV{VIPER_SKIP_WINDOWS_USER_INSTALLER_SMOKE}" STREQUAL "1")
    message(STATUS "Skipping Windows user installer smoke because VIPER_SKIP_WINDOWS_USER_INSTALLER_SMOKE=1")
    return()
endif ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}/project")
file(WRITE "${TEST_WORK_DIR}/project/main.zia" "module VAPSUserSmoke;\n\nfunc start() {}\n")
file(WRITE "${TEST_WORK_DIR}/project/viper.project"
"project vaps_user_smoke
version 1.0.0
lang zia
entry main.zia
package-name \"VAPS User Smoke\"
package-author \"Viper Project\"
package-homepage https://example.invalid/vaps-user-smoke
package-identifier org.viper.smoke.user
windows-install-scope user
windows-install-dir VAPSUserSmoke
shortcut-menu on
shortcut-desktop on
file-assoc .vapsuser \"VAPS User Smoke Source\" text/x-vaps-user --open-source
")

execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "Remove-Item -LiteralPath (Join-Path $env:LOCALAPPDATA 'VAPSUserSmoke') -Recurse -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Desktop')) 'VAPS User Smoke.lnk') -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Programs')) 'VAPSUserSmoke') -Recurse -Force -ErrorAction SilentlyContinue; reg delete 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.viper.smoke.user' /f 2>$null; reg delete 'HKCU\\Software\\Classes\\.vapsuser' /f 2>$null; reg delete 'HKCU\\Software\\Classes\\org.viper.smoke.user.vapsuser' /f 2>$null; exit 0"
        RESULT_VARIABLE _cleanup_rv)
if (NOT _cleanup_rv EQUAL 0)
    message(FATAL_ERROR "Windows user smoke pre-cleanup failed")
endif ()

set(_installer "${TEST_WORK_DIR}/vaps-user-smoke-setup.exe")
execute_process(
        COMMAND "${VIPER_BIN}" package "${TEST_WORK_DIR}/project" --target windows -o "${_installer}" --verbose
        RESULT_VARIABLE _package_rv
        OUTPUT_VARIABLE _package_out
        ERROR_VARIABLE _package_err
        TIMEOUT 180)
if (NOT _package_rv EQUAL 0)
    message(FATAL_ERROR "Windows user package build failed\nstdout:\n${_package_out}\nstderr:\n${_package_err}")
endif ()

execute_process(
        COMMAND "${_installer}" /quiet /norestart
        RESULT_VARIABLE _install_rv
        OUTPUT_VARIABLE _install_out
        ERROR_VARIABLE _install_err
        TIMEOUT 120)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR "Windows user installer failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

set(_install_root "$ENV{LOCALAPPDATA}/VAPSUserSmoke")
set(_app_exe "${_install_root}/vaps_user_smoke.exe")
set(_uninstall_exe "${_install_root}/uninstall.exe")
if (NOT EXISTS "${_app_exe}")
    message(FATAL_ERROR "Installed user app executable is missing: ${_app_exe}")
endif ()
if (NOT EXISTS "${_uninstall_exe}")
    message(FATAL_ERROR "Installed user uninstaller is missing: ${_uninstall_exe}")
endif ()

execute_process(
        COMMAND "${_app_exe}"
        RESULT_VARIABLE _app_rv
        OUTPUT_VARIABLE _app_out
        ERROR_VARIABLE _app_err
        TIMEOUT 30)
if (NOT _app_rv EQUAL 0)
    message(FATAL_ERROR "Installed user app failed to launch\nstdout:\n${_app_out}\nstderr:\n${_app_err}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.viper.smoke.user"
        RESULT_VARIABLE _reg_rv
        OUTPUT_VARIABLE _reg_out
        ERROR_VARIABLE _reg_err)
if (NOT _reg_rv EQUAL 0)
    message(FATAL_ERROR "User uninstall registry key missing\nstdout:\n${_reg_out}\nstderr:\n${_reg_err}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Classes\\.vapsuser\\OpenWithProgids"
        RESULT_VARIABLE _assoc_rv
        OUTPUT_VARIABLE _assoc_out
        ERROR_VARIABLE _assoc_err)
if (NOT _assoc_rv EQUAL 0)
    message(FATAL_ERROR "User file association key missing\nstdout:\n${_assoc_out}\nstderr:\n${_assoc_err}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Classes\\org.viper.smoke.user.vapsuser\\shell\\open\\command" /ve
        RESULT_VARIABLE _cmd_rv
        OUTPUT_VARIABLE _cmd_out
        ERROR_VARIABLE _cmd_err)
if (NOT _cmd_rv EQUAL 0)
    message(FATAL_ERROR "User file association command query failed\nstdout:\n${_cmd_out}\nstderr:\n${_cmd_err}")
endif ()
if (NOT _cmd_out MATCHES "--open-source")
    message(FATAL_ERROR "User file association command missing open args\nstdout:\n${_cmd_out}\nstderr:\n${_cmd_err}")
endif ()

execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "Write-Output ([Environment]::GetFolderPath('Desktop')); Write-Output ([Environment]::GetFolderPath('Programs'))"
        RESULT_VARIABLE _folders_rv
        OUTPUT_VARIABLE _folders_out
        ERROR_VARIABLE _folders_err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT _folders_rv EQUAL 0)
    message(FATAL_ERROR "Failed to resolve user shell folders\nstdout:\n${_folders_out}\nstderr:\n${_folders_err}")
endif ()
string(REPLACE "\r\n" "\n" _folders_out "${_folders_out}")
string(REPLACE "\r" "\n" _folders_out "${_folders_out}")
string(REGEX MATCH "([^\n]+)\n([^\n]+)" _folder_match "${_folders_out}")
set(_desktop_dir "${CMAKE_MATCH_1}")
set(_programs_dir "${CMAKE_MATCH_2}")
set(_desktop_lnk "${_desktop_dir}/VAPS User Smoke.lnk")
set(_menu_dir "${_programs_dir}/VAPSUserSmoke")
set(_menu_lnk "${_menu_dir}/VAPS User Smoke.lnk")
if (NOT EXISTS "${_desktop_lnk}")
    message(FATAL_ERROR "Desktop shortcut is missing: ${_desktop_lnk}")
endif ()
if (NOT EXISTS "${_menu_lnk}")
    message(FATAL_ERROR "Start Menu shortcut is missing: ${_menu_lnk}")
endif ()

execute_process(
        COMMAND "${_uninstall_exe}" /quiet /norestart
        RESULT_VARIABLE _uninstall_rv
        OUTPUT_VARIABLE _uninstall_out
        ERROR_VARIABLE _uninstall_err
        TIMEOUT 120)
if (NOT _uninstall_rv EQUAL 0)
    message(FATAL_ERROR "Windows user uninstaller failed\nstdout:\n${_uninstall_out}\nstderr:\n${_uninstall_err}")
endif ()

if (EXISTS "${_app_exe}")
    message(FATAL_ERROR "Installed user app executable remained after uninstall: ${_app_exe}")
endif ()
if (EXISTS "${_desktop_lnk}")
    message(FATAL_ERROR "Desktop shortcut remained after uninstall: ${_desktop_lnk}")
endif ()
if (EXISTS "${_menu_lnk}")
    message(FATAL_ERROR "Start Menu shortcut remained after uninstall: ${_menu_lnk}")
endif ()
if (EXISTS "${_menu_dir}")
    message(FATAL_ERROR "Start Menu directory remained after uninstall: ${_menu_dir}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.viper.smoke.user"
        RESULT_VARIABLE _reg_after_rv
        OUTPUT_VARIABLE _reg_after_out
        ERROR_VARIABLE _reg_after_err)
if (_reg_after_rv EQUAL 0)
    message(FATAL_ERROR "User uninstall registry key remained after uninstall\nstdout:\n${_reg_after_out}\nstderr:\n${_reg_after_err}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Classes\\org.viper.smoke.user.vapsuser"
        RESULT_VARIABLE _prog_after_rv
        OUTPUT_VARIABLE _prog_after_out
        ERROR_VARIABLE _prog_after_err)
if (_prog_after_rv EQUAL 0)
    message(FATAL_ERROR "User ProgID registry key remained after uninstall\nstdout:\n${_prog_after_out}\nstderr:\n${_prog_after_err}")
endif ()
