#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/tools/WindowsUserInstallerSmoke.cmake
# Purpose: Exercise a current-user native Windows application package lifecycle.
#
# Key invariants: Setup and detached uninstall leave no owned product state.
#
# Ownership/Lifetime: Test product state is isolated by a dedicated identifier.
#
# Links: WindowsPackageBuilder.cpp, WindowsInstallerLifecycle.cpp
#
#===----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.20)

foreach (_required ZANNA_BIN TEST_WORK_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to WindowsUserInstallerSmoke.cmake")
    endif ()
endforeach ()

if (NOT WIN32)
    message(STATUS "Skipping Windows user installer smoke on non-Windows host")
    return()
endif ()

if ("$ENV{ZANNA_SKIP_WINDOWS_USER_INSTALLER_SMOKE}" STREQUAL "1")
    message(STATUS "Skipping Windows user installer smoke because ZANNA_SKIP_WINDOWS_USER_INSTALLER_SMOKE=1")
    return()
endif ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}/project")
file(WRITE "${TEST_WORK_DIR}/project/main.zia" "module ZAPSUserSmoke;\n\nfunc start() {}\n")
file(WRITE "${TEST_WORK_DIR}/project/zanna.project"
        "project zaps_user_smoke
version 1.0.0
lang zia
entry main.zia
package-name \"ZAPS User Smoke\"
package-author \"Zanna Project\"
package-homepage https://example.invalid/zaps-user-smoke
package-identifier org.zanna.smoke.user
windows-install-scope user
windows-install-dir ZAPSUserSmoke
shortcut-menu on
shortcut-desktop on
file-assoc .zapsuser \"ZAPS User Smoke Source\" text/x-zaps-user --open-source
")

execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "$arpPath='Registry::HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.user'; $arp=Get-ItemProperty -LiteralPath $arpPath -ErrorAction SilentlyContinue; $cache=if($arp -and $arp.ZannaMaintenanceCache){[string]$arp.ZannaMaintenanceCache}else{''}; $newRoot=Join-Path $env:LOCALAPPDATA 'Programs\\ZAPSUserSmoke'; $oldRoot=Join-Path $env:LOCALAPPDATA 'ZAPSUserSmoke'; $candidate=if($cache -and (Test-Path -LiteralPath $cache -PathType Leaf)){$cache}elseif(Test-Path -LiteralPath (Join-Path $newRoot 'uninstall.exe') -PathType Leaf){Join-Path $newRoot 'uninstall.exe'}elseif(Test-Path -LiteralPath (Join-Path $oldRoot 'uninstall.exe') -PathType Leaf){Join-Path $oldRoot 'uninstall.exe'}else{$null}; if($candidate){$p=Start-Process -FilePath $candidate -ArgumentList @('/uninstall','/quiet','/norestart') -PassThru -Wait; if($p.ExitCode -notin @(0,3010)){throw \"Prior test uninstall returned $($p.ExitCode)\"}}; $deadline=[DateTime]::UtcNow.AddSeconds(45); while([DateTime]::UtcNow -lt $deadline -and ((Test-Path -LiteralPath $arpPath) -or ($cache -and (Test-Path -LiteralPath $cache)))){Start-Sleep -Milliseconds 100}; if($cache -and (Test-Path -LiteralPath $cache)){throw \"Prior test maintenance cache remained: $cache\"}; Remove-Item -LiteralPath $newRoot,$oldRoot -Recurse -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Desktop')) 'ZAPS User Smoke.lnk') -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Programs')) 'ZAPSUserSmoke') -Recurse -Force -ErrorAction SilentlyContinue; reg delete 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.user' /f 2>$null; reg delete 'HKCU\\Software\\Classes\\.zapsuser' /f 2>$null; reg delete 'HKCU\\Software\\Classes\\org.zanna.smoke.user.zapsuser' /f 2>$null; exit 0"
        RESULT_VARIABLE _cleanup_rv)
if (NOT _cleanup_rv EQUAL 0)
    message(FATAL_ERROR "Windows user smoke pre-cleanup failed")
endif ()

set(_installer "${TEST_WORK_DIR}/zaps-user-smoke-setup.exe")
execute_process(
        COMMAND "${ZANNA_BIN}" package "${TEST_WORK_DIR}/project" --target windows -o "${_installer}" --verbose
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

set(_install_root "$ENV{LOCALAPPDATA}/Programs/ZAPSUserSmoke")
set(_app_exe "${_install_root}/zaps_user_smoke.exe")
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
        COMMAND reg query "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.user"
        RESULT_VARIABLE _reg_rv
        OUTPUT_VARIABLE _reg_out
        ERROR_VARIABLE _reg_err)
if (NOT _reg_rv EQUAL 0)
    message(FATAL_ERROR "User uninstall registry key missing\nstdout:\n${_reg_out}\nstderr:\n${_reg_err}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Classes\\.zapsuser\\OpenWithProgids"
        RESULT_VARIABLE _assoc_rv
        OUTPUT_VARIABLE _assoc_out
        ERROR_VARIABLE _assoc_err)
if (NOT _assoc_rv EQUAL 0)
    message(FATAL_ERROR "User file association key missing\nstdout:\n${_assoc_out}\nstderr:\n${_assoc_err}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Classes\\org.zanna.smoke.user.zapsuser\\shell\\open\\command" /ve
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
set(_desktop_lnk "${_desktop_dir}/ZAPS User Smoke.lnk")
set(_menu_dir "${_programs_dir}/ZAPSUserSmoke")
set(_menu_lnk "${_menu_dir}/ZAPS User Smoke.lnk")
if (NOT EXISTS "${_desktop_lnk}")
    message(FATAL_ERROR "Desktop shortcut is missing: ${_desktop_lnk}")
endif ()
if (NOT EXISTS "${_menu_lnk}")
    message(FATAL_ERROR "Start Menu shortcut is missing: ${_menu_lnk}")
endif ()

execute_process(
        COMMAND powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command
        "(Get-ItemProperty -LiteralPath 'Registry::HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.user').ZannaMaintenanceCache"
        RESULT_VARIABLE _cache_query_rv
        OUTPUT_VARIABLE _maintenance_cache
        ERROR_VARIABLE _cache_query_err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT _cache_query_rv EQUAL 0 OR _maintenance_cache STREQUAL "")
    message(FATAL_ERROR "Cannot locate user-app maintenance cache\n${_cache_query_err}")
endif ()
file(TO_NATIVE_PATH "${_install_root}" _install_root_native)
file(TO_NATIVE_PATH "${_maintenance_cache}" _maintenance_cache_native)

execute_process(
        COMMAND "${_uninstall_exe}" /quiet /norestart
        RESULT_VARIABLE _uninstall_rv
        OUTPUT_VARIABLE _uninstall_out
        ERROR_VARIABLE _uninstall_err
        TIMEOUT 120)
if (NOT _uninstall_rv EQUAL 0)
    message(FATAL_ERROR "Windows user uninstaller failed\nstdout:\n${_uninstall_out}\nstderr:\n${_uninstall_err}")
endif ()
execute_process(
        COMMAND powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command
        "$deadline=[DateTime]::UtcNow.AddSeconds(45); $arp='Registry::HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.user'; while([DateTime]::UtcNow -lt $deadline -and ((Test-Path -LiteralPath '${_install_root_native}') -or (Test-Path -LiteralPath '${_maintenance_cache_native}') -or (Test-Path -LiteralPath $arp))){Start-Sleep -Milliseconds 100}; if((Test-Path -LiteralPath '${_install_root_native}') -or (Test-Path -LiteralPath '${_maintenance_cache_native}') -or (Test-Path -LiteralPath $arp)){exit 1}"
        RESULT_VARIABLE _cleanup_wait_rv)
if (NOT _cleanup_wait_rv EQUAL 0)
    message(FATAL_ERROR "Detached user-app cleanup did not converge")
endif ()
get_filename_component(_maintenance_cache_leaf "${_maintenance_cache}" DIRECTORY)
if (EXISTS "${_maintenance_cache_leaf}")
    message(FATAL_ERROR "User-app package cache leaf remained: ${_maintenance_cache_leaf}")
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
        COMMAND reg query "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.user"
        RESULT_VARIABLE _reg_after_rv
        OUTPUT_VARIABLE _reg_after_out
        ERROR_VARIABLE _reg_after_err)
if (_reg_after_rv EQUAL 0)
    message(FATAL_ERROR "User uninstall registry key remained after uninstall\nstdout:\n${_reg_after_out}\nstderr:\n${_reg_after_err}")
endif ()

execute_process(
        COMMAND reg query "HKCU\\Software\\Classes\\org.zanna.smoke.user.zapsuser"
        RESULT_VARIABLE _prog_after_rv
        OUTPUT_VARIABLE _prog_after_out
        ERROR_VARIABLE _prog_after_err)
if (_prog_after_rv EQUAL 0)
    message(FATAL_ERROR "User ProgID registry key remained after uninstall\nstdout:\n${_prog_after_out}\nstderr:\n${_prog_after_err}")
endif ()
