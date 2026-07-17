#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/tools/WindowsInstallerSmoke.cmake
# Purpose: Exercise an elevated all-users native Windows application lifecycle.
#
# Key invariants: The opt-in test owns one dedicated machine-scope product id.
#
# Ownership/Lifetime: Product state is removed through its maintenance cache.
#
# Links: WindowsPackageBuilder.cpp, WindowsInstallerLifecycle.cpp
#
#===----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.20)

foreach (_required ZANNA_BIN TEST_WORK_DIR)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to WindowsInstallerSmoke.cmake")
    endif ()
endforeach ()

if (NOT WIN32)
    message(STATUS "Skipping Windows installer smoke on non-Windows host")
    return()
endif ()

if (NOT "$ENV{ZANNA_RUN_WINDOWS_INSTALLER_SMOKE}" STREQUAL "1")
    message(STATUS "Skipping elevated Windows installer smoke; set ZANNA_RUN_WINDOWS_INSTALLER_SMOKE=1 to enable")
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
file(WRITE "${TEST_WORK_DIR}/project/main.zia" "module ZAPSSmoke;\n\nfunc start() {}\n")
file(WRITE "${TEST_WORK_DIR}/project/zanna.project"
        "project zaps_smoke
version 1.0.0
lang zia
entry main.zia
package-name \"ZAPS Smoke\"
package-author \"Zanna Project\"
package-homepage https://example.invalid/zaps-smoke
package-identifier org.zanna.smoke.install
windows-install-scope machine
shortcut-menu on
shortcut-desktop off
file-assoc .zapsmoke \"ZAPS Smoke Source\" text/x-zaps-smoke
")

execute_process(
        COMMAND powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command
        "$arpPath='Registry::HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.install'; $arp=Get-ItemProperty -LiteralPath $arpPath -ErrorAction SilentlyContinue; $cache=if($arp -and $arp.ZannaMaintenanceCache){[string]$arp.ZannaMaintenanceCache}else{''}; $root=Join-Path $env:ProgramFiles 'ZAPS Smoke'; $candidate=if($cache -and (Test-Path -LiteralPath $cache -PathType Leaf)){$cache}elseif(Test-Path -LiteralPath (Join-Path $root 'uninstall.exe') -PathType Leaf){Join-Path $root 'uninstall.exe'}else{$null}; if($candidate){$p=Start-Process -FilePath $candidate -ArgumentList @('/uninstall','/quiet','/norestart') -PassThru -Wait; if($p.ExitCode -notin @(0,3010)){throw \"Prior elevated test uninstall returned $($p.ExitCode)\"}}; $deadline=[DateTime]::UtcNow.AddSeconds(45); while([DateTime]::UtcNow -lt $deadline -and ((Test-Path -LiteralPath $arpPath) -or ($cache -and (Test-Path -LiteralPath $cache)))){Start-Sleep -Milliseconds 100}; if($cache -and (Test-Path -LiteralPath $cache)){throw \"Prior elevated test maintenance cache remained: $cache\"}; Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'ZAPS Smoke') -Recurse -Force -ErrorAction SilentlyContinue; reg delete 'HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.install' /f 2>$null; reg delete 'HKLM\\Software\\Classes\\.zapsmoke' /f 2>$null; reg delete 'HKLM\\Software\\Classes\\org.zanna.smoke.install.zapsmoke' /f 2>$null; exit 0"
        RESULT_VARIABLE _cleanup_rv)
if (NOT _cleanup_rv EQUAL 0)
    message(FATAL_ERROR "Elevated Windows smoke pre-cleanup failed")
endif ()

set(_installer "${TEST_WORK_DIR}/zaps-smoke-setup.exe")
execute_process(
        COMMAND "${ZANNA_BIN}" package "${TEST_WORK_DIR}/project" --target windows -o "${_installer}" --verbose
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

set(_install_root "$ENV{ProgramFiles}/ZAPS Smoke")
set(_app_exe "${_install_root}/zaps_smoke.exe")
set(_uninstall_exe "${_install_root}/uninstall.exe")
if (NOT EXISTS "${_app_exe}")
    message(FATAL_ERROR "Installed app executable is missing: ${_app_exe}")
endif ()
if (NOT EXISTS "${_uninstall_exe}")
    message(FATAL_ERROR "Installed uninstaller is missing: ${_uninstall_exe}")
endif ()

execute_process(
        COMMAND reg query "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.install"
        RESULT_VARIABLE _reg_rv
        OUTPUT_VARIABLE _reg_out
        ERROR_VARIABLE _reg_err)
if (NOT _reg_rv EQUAL 0)
    message(FATAL_ERROR "Uninstall registry key missing\nstdout:\n${_reg_out}\nstderr:\n${_reg_err}")
endif ()

foreach (_arp_value QuietUninstallString DisplayIcon EstimatedSize InstallDate URLInfoAbout)
    execute_process(
            COMMAND reg query "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.install" /v "${_arp_value}"
            RESULT_VARIABLE _arp_rv
            OUTPUT_VARIABLE _arp_out
            ERROR_VARIABLE _arp_err)
    if (NOT _arp_rv EQUAL 0)
        message(FATAL_ERROR "Uninstall registry value ${_arp_value} missing\nstdout:\n${_arp_out}\nstderr:\n${_arp_err}")
    endif ()
endforeach ()

execute_process(
        COMMAND powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command
        "(Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.install').ZannaMaintenanceCache"
        RESULT_VARIABLE _cache_query_rv
        OUTPUT_VARIABLE _maintenance_cache
        ERROR_VARIABLE _cache_query_err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT _cache_query_rv EQUAL 0 OR _maintenance_cache STREQUAL "")
    message(FATAL_ERROR "Cannot locate elevated-app maintenance cache\n${_cache_query_err}")
endif ()
file(TO_NATIVE_PATH "${_install_root}" _install_root_native)
file(TO_NATIVE_PATH "${_maintenance_cache}" _maintenance_cache_native)

execute_process(
        COMMAND reg query "HKLM\\Software\\Classes\\.zapsmoke\\OpenWithProgids"
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
execute_process(
        COMMAND powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command
        "$deadline=[DateTime]::UtcNow.AddSeconds(45); $arp='Registry::HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.install'; while([DateTime]::UtcNow -lt $deadline -and ((Test-Path -LiteralPath '${_install_root_native}') -or (Test-Path -LiteralPath '${_maintenance_cache_native}') -or (Test-Path -LiteralPath $arp))){Start-Sleep -Milliseconds 100}; if((Test-Path -LiteralPath '${_install_root_native}') -or (Test-Path -LiteralPath '${_maintenance_cache_native}') -or (Test-Path -LiteralPath $arp)){exit 1}"
        RESULT_VARIABLE _cleanup_wait_rv)
if (NOT _cleanup_wait_rv EQUAL 0)
    message(FATAL_ERROR "Detached elevated-app cleanup did not converge")
endif ()
get_filename_component(_maintenance_cache_leaf "${_maintenance_cache}" DIRECTORY)
if (EXISTS "${_maintenance_cache_leaf}")
    message(FATAL_ERROR "Elevated-app package cache leaf remained: ${_maintenance_cache_leaf}")
endif ()

if (EXISTS "${_app_exe}")
    message(FATAL_ERROR "Installed app executable remained after uninstall: ${_app_exe}")
endif ()

execute_process(
        COMMAND reg query "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\org.zanna.smoke.install"
        RESULT_VARIABLE _reg_after_rv
        OUTPUT_VARIABLE _reg_after_out
        ERROR_VARIABLE _reg_after_err)
if (_reg_after_rv EQUAL 0)
    message(FATAL_ERROR "Uninstall registry key remained after uninstall\nstdout:\n${_reg_after_out}\nstderr:\n${_reg_after_err}")
endif ()
