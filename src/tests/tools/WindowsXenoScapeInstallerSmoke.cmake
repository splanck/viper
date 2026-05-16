cmake_minimum_required(VERSION 3.20)

foreach (_required VIPER_BIN VIPER_REPO_ROOT TEST_WORK_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to WindowsXenoScapeInstallerSmoke.cmake")
    endif ()
endforeach ()

if (NOT WIN32)
    message(STATUS "Skipping XenoScape Windows installer smoke on non-Windows host")
    return()
endif ()

if ("$ENV{VIPER_SKIP_WINDOWS_XENOSCAPE_INSTALLER_SMOKE}" STREQUAL "1")
    message(STATUS "Skipping XenoScape Windows installer smoke because VIPER_SKIP_WINDOWS_XENOSCAPE_INSTALLER_SMOKE=1")
    return()
endif ()

set(_project_dir "${VIPER_REPO_ROOT}/examples/games/xenoscape")
if (NOT EXISTS "${_project_dir}/viper.project")
    message(FATAL_ERROR "XenoScape project manifest is missing: ${_project_dir}/viper.project")
endif ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "Remove-Item -LiteralPath (Join-Path $env:LOCALAPPDATA 'Xenoscape') -Recurse -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Xenoscape.lnk') -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Programs')) 'Xenoscape') -Recurse -Force -ErrorAction SilentlyContinue; reg delete 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.viper-lang.xenoscape' /f 2>$null; exit 0"
        RESULT_VARIABLE _cleanup_rv)
if (NOT _cleanup_rv EQUAL 0)
    message(FATAL_ERROR "XenoScape installer smoke pre-cleanup failed")
endif ()

set(_installer "${TEST_WORK_DIR}/xenoscape-setup.exe")
execute_process(
        COMMAND "${VIPER_BIN}" package "${_project_dir}" --target windows -o "${_installer}" --verbose
        RESULT_VARIABLE _package_rv
        OUTPUT_VARIABLE _package_out
        ERROR_VARIABLE _package_err
        TIMEOUT 300)
if (NOT _package_rv EQUAL 0)
    message(FATAL_ERROR "XenoScape Windows package build failed\nstdout:\n${_package_out}\nstderr:\n${_package_err}")
endif ()
if (NOT EXISTS "${_installer}")
    message(FATAL_ERROR "XenoScape installer was not produced: ${_installer}")
endif ()

execute_process(
        COMMAND "${_installer}" /quiet /norestart
        RESULT_VARIABLE _install_rv
        OUTPUT_VARIABLE _install_out
        ERROR_VARIABLE _install_err
        TIMEOUT 120)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR "XenoScape installer failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

set(_install_root "$ENV{LOCALAPPDATA}/Xenoscape")
set(_app_exe "${_install_root}/xenoscape.exe")
set(_uninstall_exe "${_install_root}/uninstall.exe")
set(_runtime_json "${_install_root}/xenoscape.runtime.json")
set(_sound_wav "${_install_root}/sounds/jump.wav")
foreach (_installed_file "${_app_exe}" "${_uninstall_exe}" "${_runtime_json}" "${_sound_wav}")
    if (NOT EXISTS "${_installed_file}")
        message(FATAL_ERROR "Expected XenoScape installed file is missing: ${_installed_file}")
    endif ()
endforeach ()

file(READ "${_runtime_json}" _runtime_text)
if (NOT _runtime_text MATCHES "introSplashMs")
    message(FATAL_ERROR "Installed XenoScape runtime JSON did not contain expected tuning keys")
endif ()

execute_process(
        COMMAND "${_app_exe}" --viper-package-smoke
        WORKING_DIRECTORY "${TEST_WORK_DIR}"
        RESULT_VARIABLE _app_smoke_rv
        OUTPUT_VARIABLE _app_smoke_out
        ERROR_VARIABLE _app_smoke_err
        TIMEOUT 30)
if (NOT _app_smoke_rv EQUAL 0)
    message(FATAL_ERROR "Installed XenoScape package smoke failed\nstdout:\n${_app_smoke_out}\nstderr:\n${_app_smoke_err}")
endif ()
if (NOT _app_smoke_out MATCHES "RESULT: package_smoke_ok")
    message(FATAL_ERROR "Installed XenoScape package smoke did not report success\nstdout:\n${_app_smoke_out}\nstderr:\n${_app_smoke_err}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.viper-lang.xenoscape"
        RESULT_VARIABLE _reg_rv
        OUTPUT_VARIABLE _reg_out
        ERROR_VARIABLE _reg_err)
if (NOT _reg_rv EQUAL 0)
    message(FATAL_ERROR "XenoScape user uninstall registry key missing\nstdout:\n${_reg_out}\nstderr:\n${_reg_err}")
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
set(_desktop_lnk "${_desktop_dir}/Xenoscape.lnk")
set(_menu_dir "${_programs_dir}/Xenoscape")
set(_menu_lnk "${_menu_dir}/Xenoscape.lnk")
if (NOT EXISTS "${_desktop_lnk}")
    message(FATAL_ERROR "XenoScape desktop shortcut is missing: ${_desktop_lnk}")
endif ()
if (NOT EXISTS "${_menu_lnk}")
    message(FATAL_ERROR "XenoScape Start Menu shortcut is missing: ${_menu_lnk}")
endif ()

execute_process(
        COMMAND "${_uninstall_exe}" /quiet
        RESULT_VARIABLE _uninstall_rv
        OUTPUT_VARIABLE _uninstall_out
        ERROR_VARIABLE _uninstall_err
        TIMEOUT 120)
if (NOT _uninstall_rv EQUAL 0)
    message(FATAL_ERROR "XenoScape uninstaller failed\nstdout:\n${_uninstall_out}\nstderr:\n${_uninstall_err}")
endif ()

foreach (_removed_file "${_app_exe}" "${_runtime_json}" "${_sound_wav}" "${_desktop_lnk}" "${_menu_lnk}")
    if (EXISTS "${_removed_file}")
        message(FATAL_ERROR "XenoScape file remained after uninstall: ${_removed_file}")
    endif ()
endforeach ()
if (EXISTS "${_menu_dir}")
    message(FATAL_ERROR "XenoScape Start Menu directory remained after uninstall: ${_menu_dir}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.viper-lang.xenoscape"
        RESULT_VARIABLE _reg_after_rv
        OUTPUT_VARIABLE _reg_after_out
        ERROR_VARIABLE _reg_after_err)
if (_reg_after_rv EQUAL 0)
    message(FATAL_ERROR "XenoScape uninstall registry key remained after uninstall\nstdout:\n${_reg_after_out}\nstderr:\n${_reg_after_err}")
endif ()

execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "Remove-Item -LiteralPath (Join-Path $env:LOCALAPPDATA 'Xenoscape') -Recurse -Force -ErrorAction SilentlyContinue; exit 0")
