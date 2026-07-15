cmake_minimum_required(VERSION 3.20)

foreach (_required CMAKE_BIN VIPER_BUILD_DIR VIPER_BIN)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to WindowsToolchainInstallerE2E.cmake")
    endif ()
endforeach ()

if (NOT WIN32)
    message(FATAL_ERROR "WindowsToolchainInstallerE2E.cmake must run on Windows")
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/ToolchainInstallerSmokeHelpers.cmake")

foreach (_required_environment LOCALAPPDATA APPDATA)
    if (NOT DEFINED ENV{${_required_environment}} OR "$ENV{${_required_environment}}" STREQUAL "")
        message(FATAL_ERROR
                "${_required_environment} is not set; cannot locate per-user installer paths")
    endif ()
endforeach ()

function (_viper_installer_assert_exists path context)
    if (NOT EXISTS "${path}")
        message(FATAL_ERROR "${context} is missing: ${path}")
    endif ()
endfunction ()

function (_viper_installer_assert_absent path context)
    if (EXISTS "${path}")
        message(FATAL_ERROR "${context} was not removed: ${path}")
    endif ()
endfunction ()

function (_viper_installer_assert_manifest_contains manifest needle context)
    string(FIND "${manifest}" "${needle}" _needle_offset)
    if (_needle_offset EQUAL -1)
        message(FATAL_ERROR "${context} is missing '${needle}'")
    endif ()
endfunction ()

function (_viper_installer_assert_manifest_excludes manifest needle context)
    string(FIND "${manifest}" "${needle}" _needle_offset)
    if (NOT _needle_offset EQUAL -1)
        message(FATAL_ERROR "${context} unexpectedly contains '${needle}'")
    endif ()
endfunction ()

set(_tmp_root "${VIPER_BUILD_DIR}/tests/windows-toolchain-installer-e2e")
set(_stage_dir "${_tmp_root}/stage")
set(_installer "${_tmp_root}/viper-toolchain-e2e.exe")
set(_install_dir_name "ViperInstallerE2E")
set(_package_identifier "org.viper.toolchain.e2e")
set(_install_root "$ENV{LOCALAPPDATA}/${_install_dir_name}")
set(_installed_viper "${_install_root}/bin/viper.exe")
set(_installed_viperide "${_install_root}/bin/viperide.exe")
set(_installed_viperide_icon "${_install_root}/bin/viperide.ico")
set(_installed_samples "${_install_root}/share/viper/samples")
set(_installed_vscode "${_install_root}/share/viper/vscode")
set(_installed_vscode_helper "${_install_root}/bin/viper-install-vscode-extension.cmd")
set(_developer_prompt "${_install_root}/bin/viper-dev.cmd")
set(_uninstaller "${_install_root}/uninstall.exe")
set(_installed_manifest "${_install_root}/.viper-install-manifest.txt")
set(_start_menu "$ENV{APPDATA}/Microsoft/Windows/Start Menu/Programs/${_install_dir_name}")
set(_developer_prompt_shortcut "${_start_menu}/Viper Developer Prompt.lnk")
set(_viperide_shortcut "${_start_menu}/ViperIDE.lnk")
set(_vscode_shortcut "${_start_menu}/Install VS Code Extension.lnk")
set(_setup_log "$ENV{TEMP}/ViperInstaller-${_package_identifier}.log")
set(_src_dir "${_tmp_root}/consumer-src")
set(_build_dir "${_tmp_root}/consumer-build")

file(REMOVE_RECURSE "${_tmp_root}")
file(MAKE_DIRECTORY "${_tmp_root}" "${_src_dir}")

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
            "cmake --install failed while staging toolchain installer e2e payload\nstdout:\n${_stage_out}\nstderr:\n${_stage_err}")
endif ()

file(GLOB_RECURSE _staged_sample_files LIST_DIRECTORIES FALSE
        "${_stage_dir}/share/viper/samples/*")
file(GLOB_RECURSE _staged_vscode_files LIST_DIRECTORIES FALSE
        "${_stage_dir}/share/viper/vscode/*")
file(GLOB _staged_vsix_files LIST_DIRECTORIES FALSE
        "${_stage_dir}/share/viper/vscode/*.vsix")
if (NOT EXISTS "${_stage_dir}/bin/viperide.exe")
    message(FATAL_ERROR "staged Windows toolchain omitted the ViperIDE component")
endif ()
if (NOT _staged_sample_files)
    message(FATAL_ERROR "staged Windows toolchain omitted the samples component")
endif ()
if (NOT _staged_vscode_files)
    message(FATAL_ERROR "staged Windows toolchain omitted the VS Code component")
endif ()
list(GET _staged_sample_files 0 _staged_sample_probe)
list(GET _staged_vscode_files 0 _staged_vscode_probe)
file(RELATIVE_PATH _sample_probe_relative "${_stage_dir}" "${_staged_sample_probe}")
file(RELATIVE_PATH _vscode_probe_relative "${_stage_dir}" "${_staged_vscode_probe}")
set(_installed_sample_probe "${_install_root}/${_sample_probe_relative}")
set(_installed_vscode_probe "${_install_root}/${_vscode_probe_relative}")

set(_package_cmd
        "${VIPER_BIN}" install-package
        --stage-dir "${_stage_dir}"
        --target windows
        --windows-install-scope user
        --windows-install-dir "${_install_dir_name}"
        --windows-identifier "${_package_identifier}"
        --windows-file-associations off
        --windows-shortcuts on
        -o "${_installer}")

execute_process(
        COMMAND ${_package_cmd}
        RESULT_VARIABLE _pkg_rv
        OUTPUT_VARIABLE _pkg_out
        ERROR_VARIABLE _pkg_err)
if (NOT _pkg_rv EQUAL 0)
    message(FATAL_ERROR
            "toolchain installer generation failed\nstdout:\n${_pkg_out}\nstderr:\n${_pkg_err}")
endif ()
if (NOT EXISTS "${_installer}")
    message(FATAL_ERROR "toolchain installer was not produced: ${_installer}")
endif ()

if (EXISTS "${_uninstaller}")
    execute_process(COMMAND "${_uninstaller}" /quiet /norestart
            RESULT_VARIABLE _pre_uninstall_rv
            OUTPUT_VARIABLE _pre_uninstall_out
            ERROR_VARIABLE _pre_uninstall_err)
    if (NOT _pre_uninstall_rv EQUAL 0)
        message(FATAL_ERROR
                "pre-existing Viper uninstall failed\nstdout:\n${_pre_uninstall_out}\nstderr:\n${_pre_uninstall_err}")
    endif ()
endif ()
file(REMOVE_RECURSE "${_install_root}")
file(REMOVE_RECURSE "${_start_menu}")

# A random collision must still be rejected, and the backend reason must survive in a setup log.
file(MAKE_DIRECTORY "${_install_root}/bin")
file(WRITE "${_installed_viper}" "unowned-collision-sentinel")
file(REMOVE "${_setup_log}")
execute_process(COMMAND "${_installer}" /quiet /norestart
        RESULT_VARIABLE _collision_rv
        OUTPUT_VARIABLE _collision_out
        ERROR_VARIABLE _collision_err)
if (_collision_rv EQUAL 0)
    message(FATAL_ERROR "installer replaced an unowned collision")
endif ()
file(READ "${_installed_viper}" _collision_sentinel)
if (NOT _collision_sentinel STREQUAL "unowned-collision-sentinel")
    message(FATAL_ERROR "failed install modified the unowned collision")
endif ()
if (NOT EXISTS "${_setup_log}")
    message(FATAL_ERROR "failed install did not write its diagnostic log: ${_setup_log}")
endif ()
file(READ "${_setup_log}" _collision_log)
if (NOT _collision_log MATCHES "refusing to replace unowned path")
    message(FATAL_ERROR
            "setup log omitted the collision reason\nlog:\n${_collision_log}\nstderr:\n${_collision_err}")
endif ()
file(REMOVE_RECURSE "${_install_root}")

execute_process(COMMAND "${_installer}" /quiet /norestart
        RESULT_VARIABLE _install_rv
        OUTPUT_VARIABLE _install_out
        ERROR_VARIABLE _install_err)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR
            "per-user Viper installer failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

_viper_installer_assert_exists("${_installed_viperide}" "default ViperIDE component")
_viper_installer_assert_exists("${_installed_viperide_icon}" "default ViperIDE icon")
_viper_installer_assert_exists("${_installed_sample_probe}" "default samples component")
_viper_installer_assert_exists("${_installed_vscode_probe}" "default VS Code component")
_viper_installer_assert_exists("${_installed_vscode_helper}" "default VS Code helper")
_viper_installer_assert_exists("${_developer_prompt_shortcut}" "developer prompt shortcut")
_viper_installer_assert_exists("${_viperide_shortcut}" "default ViperIDE shortcut")
if (_staged_vsix_files)
    _viper_installer_assert_exists("${_vscode_shortcut}" "default VS Code shortcut")
endif ()
file(READ "${_installed_manifest}" _default_component_manifest)
string(REPLACE "\\" "/" _default_component_manifest "${_default_component_manifest}")
_viper_installer_assert_manifest_contains(
        "${_default_component_manifest}" "bin/viperide.exe" "default component manifest")
_viper_installer_assert_manifest_contains(
        "${_default_component_manifest}" "share/viper/samples/" "default component manifest")
_viper_installer_assert_manifest_contains(
        "${_default_component_manifest}" "share/viper/vscode/" "default component manifest")

# Pre-manifest generated installers are trusted only through their generated uninstaller marker.
# This mirrors the real upgrade regression: the payload exists, but its ownership manifest and
# Add/Remove Programs registration are missing.
if (NOT EXISTS "${_installed_manifest}")
    message(FATAL_ERROR "clean install did not write ${_installed_manifest}")
endif ()
file(REMOVE "${_installed_manifest}")
execute_process(
        COMMAND "$ENV{SystemRoot}/System32/reg.exe" delete
                "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${_package_identifier}"
                /f
        RESULT_VARIABLE _legacy_reg_rv
        OUTPUT_QUIET
        ERROR_QUIET)
if (NOT _legacy_reg_rv EQUAL 0)
    message(FATAL_ERROR "failed to remove Add/Remove Programs metadata for legacy migration test")
endif ()
file(REMOVE "${_setup_log}")
execute_process(COMMAND "${_installer}" /quiet /norestart
        RESULT_VARIABLE _legacy_upgrade_rv
        OUTPUT_VARIABLE _legacy_upgrade_out
        ERROR_VARIABLE _legacy_upgrade_err)
if (NOT _legacy_upgrade_rv EQUAL 0)
    message(FATAL_ERROR
            "legacy manifest migration failed\nstdout:\n${_legacy_upgrade_out}\nstderr:\n${_legacy_upgrade_err}")
endif ()
if (NOT EXISTS "${_installed_manifest}")
    message(FATAL_ERROR "legacy migration did not restore the ownership manifest")
endif ()
file(READ "${_setup_log}" _legacy_upgrade_log)
if (NOT _legacy_upgrade_log MATCHES "migrating generated legacy installation")
    message(FATAL_ERROR "legacy migration was not recorded in the setup log:\n${_legacy_upgrade_log}")
endif ()

# Component opt-outs are upgrade-safe: stale owned files and shortcuts are removed while core
# tools and unrelated developer files remain untouched.
set(_unowned_sentinel "${_install_root}/developer-unowned-sentinel.txt")
file(WRITE "${_unowned_sentinel}" "preserve-me")
execute_process(
        COMMAND "${_installer}" /quiet /norestart /no-viperide /no-samples /no-vscode
        RESULT_VARIABLE _minimal_upgrade_rv
        OUTPUT_VARIABLE _minimal_upgrade_out
        ERROR_VARIABLE _minimal_upgrade_err)
if (NOT _minimal_upgrade_rv EQUAL 0)
    message(FATAL_ERROR
            "component opt-out upgrade failed\nstdout:\n${_minimal_upgrade_out}\nstderr:\n${_minimal_upgrade_err}")
endif ()
_viper_installer_assert_exists("${_installed_viper}" "core tool after component opt-out")
_viper_installer_assert_exists("${_developer_prompt}" "developer prompt after component opt-out")
_viper_installer_assert_exists(
        "${_developer_prompt_shortcut}" "core shortcut after component opt-out")
_viper_installer_assert_exists("${_unowned_sentinel}" "unowned developer file")
_viper_installer_assert_absent("${_installed_viperide}" "opted-out ViperIDE executable")
_viper_installer_assert_absent("${_installed_viperide_icon}" "opted-out ViperIDE icon")
_viper_installer_assert_absent("${_installed_sample_probe}" "opted-out sample")
_viper_installer_assert_absent("${_installed_vscode_probe}" "opted-out VS Code payload")
_viper_installer_assert_absent("${_installed_vscode_helper}" "opted-out VS Code helper")
_viper_installer_assert_absent("${_viperide_shortcut}" "opted-out ViperIDE shortcut")
if (_staged_vsix_files)
    _viper_installer_assert_absent("${_vscode_shortcut}" "opted-out VS Code shortcut")
endif ()
file(READ "${_installed_manifest}" _minimal_component_manifest)
string(REPLACE "\\" "/" _minimal_component_manifest "${_minimal_component_manifest}")
_viper_installer_assert_manifest_excludes(
        "${_minimal_component_manifest}" "bin/viperide.exe" "minimal component manifest")
_viper_installer_assert_manifest_excludes(
        "${_minimal_component_manifest}" "share/viper/samples/" "minimal component manifest")
_viper_installer_assert_manifest_excludes(
        "${_minimal_component_manifest}" "share/viper/vscode/" "minimal component manifest")
_viper_installer_assert_manifest_excludes(
        "${_minimal_component_manifest}"
        "bin/viper-install-vscode-extension.cmd"
        "minimal component manifest")

# A normal upgrade restores every default-enabled component and its owned shortcut.
execute_process(COMMAND "${_installer}" /quiet /norestart
        RESULT_VARIABLE _restore_upgrade_rv
        OUTPUT_VARIABLE _restore_upgrade_out
        ERROR_VARIABLE _restore_upgrade_err)
if (NOT _restore_upgrade_rv EQUAL 0)
    message(FATAL_ERROR
            "default component restore failed\nstdout:\n${_restore_upgrade_out}\nstderr:\n${_restore_upgrade_err}")
endif ()
_viper_installer_assert_exists("${_installed_viperide}" "restored ViperIDE component")
_viper_installer_assert_exists("${_installed_viperide_icon}" "restored ViperIDE icon")
_viper_installer_assert_exists("${_installed_sample_probe}" "restored samples component")
_viper_installer_assert_exists("${_installed_vscode_probe}" "restored VS Code component")
_viper_installer_assert_exists("${_installed_vscode_helper}" "restored VS Code helper")
_viper_installer_assert_exists("${_viperide_shortcut}" "restored ViperIDE shortcut")
if (_staged_vsix_files)
    _viper_installer_assert_exists("${_vscode_shortcut}" "restored VS Code shortcut")
endif ()
file(READ "${_installed_manifest}" _restored_component_manifest)
string(REPLACE "\\" "/" _restored_component_manifest "${_restored_component_manifest}")
_viper_installer_assert_manifest_contains(
        "${_restored_component_manifest}" "bin/viperide.exe" "restored component manifest")
_viper_installer_assert_manifest_contains(
        "${_restored_component_manifest}" "share/viper/samples/" "restored component manifest")
_viper_installer_assert_manifest_contains(
        "${_restored_component_manifest}" "share/viper/vscode/" "restored component manifest")
viper_installer_smoke_verify_installed_tools("${_install_root}/bin" ".exe" "Windows installer E2E")

set(_path_probe_ps [=[$machine=[Environment]::GetEnvironmentVariable('Path','Machine'); $user=[Environment]::GetEnvironmentVariable('Path','User'); $env:Path=($machine + ';' + $user); viper --version]=])
execute_process(COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -Command "${_path_probe_ps}"
        RESULT_VARIABLE _path_rv
        OUTPUT_VARIABLE _path_out
        ERROR_VARIABLE _path_err)
if (NOT _path_rv EQUAL 0)
    message(FATAL_ERROR
            "installed viper was not discoverable through a fresh registry PATH projection\nstdout:\n${_path_out}\nstderr:\n${_path_err}")
endif ()

set(_run_bas "${_tmp_root}/installer-run-smoke.bas")
file(WRITE "${_run_bas}" "10 PRINT \"INSTALLER-RUN-SMOKE\"\n")
execute_process(COMMAND "${_installed_viper}" run "${_run_bas}"
        RESULT_VARIABLE _run_rv
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err)
if (NOT _run_rv EQUAL 0)
    message(FATAL_ERROR
            "installed viper run failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()
if (NOT _run_out MATCHES "INSTALLER-RUN-SMOKE")
    message(FATAL_ERROR
            "installed viper run produced unexpected output\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()

if (DEFINED ENV{PROCESSOR_ARCHITECTURE} AND "$ENV{PROCESSOR_ARCHITECTURE}" MATCHES "^(ARM64|arm64)$")
    set(_installed_codegen_arch arm64)
else ()
    set(_installed_codegen_arch x64)
endif ()
set(_installed_il "${_tmp_root}/installer-native-smoke.il")
set(_installed_exe "${_tmp_root}/installer-native-smoke.exe")
file(WRITE "${_installed_il}" [=[
il 0.3.0

extern @Viper.Terminal.PrintStr(str) -> void
global const str @.msg = "INSTALLER-NATIVE-SMOKE"

func @main() -> i64 {
entry:
  %msg = const_str @.msg
  call @Viper.Terminal.PrintStr(%msg)
  ret 0
}
]=])
execute_process(
        COMMAND "${CMAKE_BIN}" -E env --unset=VIPER_LIB_PATH "${_installed_viper}" codegen "${_installed_codegen_arch}" "${_installed_il}" -o "${_installed_exe}"
        WORKING_DIRECTORY "${_tmp_root}"
        RESULT_VARIABLE _codegen_rv
        OUTPUT_VARIABLE _codegen_out
        ERROR_VARIABLE _codegen_err)
if (NOT _codegen_rv EQUAL 0)
    message(FATAL_ERROR
            "installed viper native codegen failed\nstdout:\n${_codegen_out}\nstderr:\n${_codegen_err}")
endif ()
if (NOT EXISTS "${_installed_exe}")
    message(FATAL_ERROR "installed viper did not produce native smoke executable: ${_installed_exe}")
endif ()
execute_process(COMMAND "${_installed_exe}"
        WORKING_DIRECTORY "${_tmp_root}"
        RESULT_VARIABLE _native_rv
        OUTPUT_VARIABLE _native_out
        ERROR_VARIABLE _native_err)
if (NOT _native_rv EQUAL 0)
    message(FATAL_ERROR
            "native executable built by installed viper failed\nstdout:\n${_native_out}\nstderr:\n${_native_err}")
endif ()
if (NOT _native_out MATCHES "INSTALLER-NATIVE-SMOKE")
    message(FATAL_ERROR
            "native executable built by installed viper produced unexpected output\nstdout:\n${_native_out}\nstderr:\n${_native_err}")
endif ()

file(WRITE "${_src_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(viper_installer_e2e_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Viper CONFIG REQUIRED)
add_executable(viper_installer_e2e_consumer main.cpp)
target_link_libraries(viper_installer_e2e_consumer PRIVATE viper::il_core viper::il_io)
]=])

file(WRITE "${_src_dir}/main.cpp" [=[
#include <sstream>
#include <viper/il/core/Module.hpp>
#include <viper/il/io/Serializer.hpp>

int main() {
    il::core::Module module;
    std::ostringstream out;
    il::io::Serializer::write(module, out);
    return out.str().empty() ? 1 : 0;
}
]=])

if (NOT EXISTS "${_developer_prompt}")
    message(FATAL_ERROR "installer did not install the developer prompt: ${_developer_prompt}")
endif ()
set(_consumer_configure_cmd "${_tmp_root}/configure-consumer.cmd")
file(WRITE "${_consumer_configure_cmd}"
        "@echo off\r\n"
        "call \"${_developer_prompt}\"\r\n"
        "if errorlevel 1 exit /b %errorlevel%\r\n"
        "\"${CMAKE_BIN}\" -S \"${_src_dir}\" -B \"${_build_dir}\"\r\n"
        "exit /b %errorlevel%\r\n")
execute_process(
        COMMAND cmd.exe /d /c "${_consumer_configure_cmd}"
        RESULT_VARIABLE _cfg_rv
        OUTPUT_VARIABLE _cfg_out
        ERROR_VARIABLE _cfg_err)
if (NOT _cfg_rv EQUAL 0)
    message(FATAL_ERROR
            "external find_package(Viper) configure failed against installed toolchain\nstdout:\n${_cfg_out}\nstderr:\n${_cfg_err}")
endif ()

set(_consumer_build_cmd "${CMAKE_BIN}" --build "${_build_dir}")
if (DEFINED VIPER_CONFIG AND NOT "${VIPER_CONFIG}" STREQUAL "")
    list(APPEND _consumer_build_cmd --config "${VIPER_CONFIG}")
endif ()
execute_process(
        COMMAND ${_consumer_build_cmd}
        RESULT_VARIABLE _build_rv
        OUTPUT_VARIABLE _build_out
        ERROR_VARIABLE _build_err)
if (NOT _build_rv EQUAL 0)
    message(FATAL_ERROR
            "external consumer build failed against installed toolchain\nstdout:\n${_build_out}\nstderr:\n${_build_err}")
endif ()

if (NOT EXISTS "${_uninstaller}")
    message(FATAL_ERROR "installer did not install uninstall.exe: ${_uninstaller}")
endif ()
execute_process(COMMAND "${_uninstaller}" /quiet /norestart
        RESULT_VARIABLE _uninstall_rv
        OUTPUT_VARIABLE _uninstall_out
        ERROR_VARIABLE _uninstall_err)
if (NOT _uninstall_rv EQUAL 0)
    message(FATAL_ERROR
            "per-user Viper uninstall failed\nstdout:\n${_uninstall_out}\nstderr:\n${_uninstall_err}")
endif ()
if (EXISTS "${_installed_viper}")
    message(FATAL_ERROR "uninstaller left viper.exe behind: ${_installed_viper}")
endif ()
_viper_installer_assert_absent("${_viperide_shortcut}" "uninstalled ViperIDE shortcut")
_viper_installer_assert_absent(
        "${_developer_prompt_shortcut}" "uninstalled developer prompt shortcut")
if (_staged_vsix_files)
    _viper_installer_assert_absent("${_vscode_shortcut}" "uninstalled VS Code shortcut")
endif ()
file(REMOVE "${_uninstaller}")
file(REMOVE_RECURSE "${_install_root}")
file(REMOVE_RECURSE "${_start_menu}")
