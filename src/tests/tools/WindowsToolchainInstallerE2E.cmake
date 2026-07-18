#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/tools/WindowsToolchainInstallerE2E.cmake
# Purpose: Validate the native Windows toolchain installer lifecycle end to end.
#
# Key invariants: Every test product is isolated and removed with owned state only.
#
# Ownership/Lifetime: Test packages and installs stay under ZANNA_BUILD_DIR.
#
# Links: WindowsInstallerLifecycle.cpp, docs/installer-release.md
#
#===----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.20)

foreach (_required CMAKE_BIN ZANNA_BUILD_DIR ZANNA_BIN)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must be provided to WindowsToolchainInstallerE2E.cmake")
    endif ()
endforeach ()

if (NOT WIN32)
    message(FATAL_ERROR "WindowsToolchainInstallerE2E.cmake must run on Windows")
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/ToolchainInstallerSmokeHelpers.cmake")
string(ASCII 239 187 191 _utf8_bom)

foreach (_required_environment LOCALAPPDATA APPDATA)
    if (NOT DEFINED ENV{${_required_environment}} OR "$ENV{${_required_environment}}" STREQUAL "")
        message(FATAL_ERROR
                "${_required_environment} is not set; cannot locate per-user installer paths")
    endif ()
endforeach ()

function (_zanna_installer_assert_exists path context)
    if (NOT EXISTS "${path}")
        message(FATAL_ERROR "${context} is missing: ${path}")
    endif ()
endfunction ()

function (_zanna_installer_assert_absent path context)
    if (EXISTS "${path}")
        message(FATAL_ERROR "${context} was not removed: ${path}")
    endif ()
endfunction ()

function (_zanna_installer_assert_manifest_contains manifest needle context)
    string(FIND "${manifest}" "${needle}" _needle_offset)
    if (_needle_offset EQUAL -1)
        message(FATAL_ERROR "${context} is missing '${needle}'")
    endif ()
endfunction ()

function (_zanna_installer_assert_manifest_excludes manifest needle context)
    string(FIND "${manifest}" "${needle}" _needle_offset)
    if (NOT _needle_offset EQUAL -1)
        message(FATAL_ERROR "${context} unexpectedly contains '${needle}'")
    endif ()
endfunction ()

set(_tmp_root "${ZANNA_BUILD_DIR}/tests/windows-toolchain-installer-e2e")
set(_stage_dir "${_tmp_root}/stage")
set(_installer "${_tmp_root}/zanna-toolchain-e2e.exe")
set(_installer_repeat "${_tmp_root}/zanna-toolchain-e2e-repeat.exe")
set(_install_dir_name "ZannaInstallerE2E")
set(_package_identifier "org.zanna.toolchain.e2e")
set(_install_root "${_tmp_root}/install-Δ Space")
set(_installed_zanna "${_install_root}/bin/zanna.exe")
set(_installed_zannastudio "${_install_root}/bin/zannastudio.exe")
set(_installed_zannastudio_icon "${_install_root}/bin/zannastudio.ico")
set(_installed_samples "${_install_root}/share/zanna/samples")
set(_installed_vscode "${_install_root}/share/zanna/vscode")
set(_installed_vscode_helper "${_install_root}/bin/zanna-install-vscode-extension.cmd")
set(_developer_prompt "${_install_root}/bin/zanna-dev.cmd")
set(_uninstaller "${_install_root}/uninstall.exe")
set(_installed_manifest "${_install_root}/.zanna-install-manifest.txt")
set(_start_menu "$ENV{APPDATA}/Microsoft/Windows/Start Menu/Programs/${_install_dir_name}")
set(_developer_prompt_shortcut "${_start_menu}/Zanna Developer Prompt.lnk")
set(_zannastudio_shortcut "${_start_menu}/Zanna Studio.lnk")
set(_vscode_shortcut "${_start_menu}/Install VS Code Extension.lnk")
set(_setup_log "${_tmp_root}/installer-session.log")
set(_src_dir "${_tmp_root}/consumer-src")
set(_build_dir "${_tmp_root}/consumer-build")

# Recover from an aborted prior opt-in E2E run before deleting its in-tree
# uninstaller. Final assertions below still require the production uninstaller
# to remove every owned integration without this test-only cleanup.
file(TO_NATIVE_PATH "${_install_root}" _preclean_install_root)
set(_preclean_script "${ZANNA_BUILD_DIR}/tests/windows-toolchain-installer-e2e-preclean.ps1")
file(WRITE "${_preclean_script}"
        "${_utf8_bom}"
        "$ErrorActionPreference = 'Stop'\n"
        "$identifier = '${_package_identifier}'\n"
        "$root = '${_preclean_install_root}'\n"
        "$arpPath = \"Registry::HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$identifier\"\n"
        "$arp = Get-ItemProperty -LiteralPath $arpPath -ErrorAction SilentlyContinue\n"
        "$candidate = if ($arp -and (Test-Path -LiteralPath $arp.ZannaMaintenanceCache -PathType Leaf)) { $arp.ZannaMaintenanceCache } elseif (Test-Path -LiteralPath ([IO.Path]::Combine($root, 'uninstall.exe')) -PathType Leaf) { [IO.Path]::Combine($root, 'uninstall.exe') } else { $null }\n"
        "if ($candidate) { $process = Start-Process -FilePath $candidate -ArgumentList @('/uninstall','/quiet','/norestart') -PassThru -Wait; if ($process.ExitCode -notin @(0, 3010)) { throw \"Prior E2E uninstall returned $($process.ExitCode)\" } }\n"
        "$deadline = [DateTime]::UtcNow.AddSeconds(30)\n"
        "while ([DateTime]::UtcNow -lt $deadline -and (Test-Path -LiteralPath $arpPath)) { Start-Sleep -Milliseconds 100 }\n"
        "Remove-Item -LiteralPath $arpPath -Recurse -Force -ErrorAction SilentlyContinue\n"
        "$bin = [IO.Path]::Combine($root, 'bin')\n"
        "$path = [Environment]::GetEnvironmentVariable('Path', 'User')\n"
        "$entries = @($path -split ';' | Where-Object { $_ -and -not [string]::Equals($_.TrimEnd('\\'), $bin.TrimEnd('\\'), [StringComparison]::OrdinalIgnoreCase) })\n"
        "[Environment]::SetEnvironmentVariable('Path', ($entries -join ';'), 'User')\n"
        "foreach ($extension in @('zia','bas','il')) { $progId = \"$identifier.$extension\"; Remove-Item -LiteralPath \"Registry::HKEY_CURRENT_USER\\Software\\Classes\\$progId\" -Recurse -Force -ErrorAction SilentlyContinue; $openWith = \"Registry::HKEY_CURRENT_USER\\Software\\Classes\\.$extension\\OpenWithProgids\"; if (Test-Path -LiteralPath $openWith) { Remove-ItemProperty -LiteralPath $openWith -Name $progId -Force -ErrorAction SilentlyContinue } }\n")
execute_process(
        COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
                -File "${_preclean_script}"
        RESULT_VARIABLE _preclean_rv
        ERROR_VARIABLE _preclean_err)
if (NOT _preclean_rv EQUAL 0)
    message(FATAL_ERROR "cannot clean a prior Windows installer E2E run\n${_preclean_err}")
endif ()
file(REMOVE_RECURSE "${_tmp_root}")
file(REMOVE_RECURSE "${_start_menu}")
file(MAKE_DIRECTORY "${_tmp_root}" "${_src_dir}")

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
            "cmake --install failed while staging toolchain installer e2e payload\nstdout:\n${_stage_out}\nstderr:\n${_stage_err}")
endif ()

if (ZANNA_CONFIG STREQUAL "Debug")
    # InstallRequiredSystemLibraries deliberately excludes non-redistributable
    # Debug CRT files. This opt-in lifecycle test packages only into its owned
    # build-tree sandbox, so stage the matching Debug CRT closure explicitly.
    if (NOT DEFINED ZANNA_MSVC_REDIST_DIR OR
        "${ZANNA_MSVC_REDIST_DIR}" STREQUAL "" OR
        NOT IS_DIRECTORY "${ZANNA_MSVC_REDIST_DIR}")
        message(FATAL_ERROR "MSVC redistributable directory is unavailable for Debug E2E staging")
    endif ()
    string(TOLOWER "${ZANNA_SYSTEM_PROCESSOR}" _system_processor)
    if (_system_processor MATCHES "^(arm64|aarch64)$")
        set(_debug_crt_arch arm64)
    elseif (_system_processor MATCHES "^(amd64|x86_64|x64)$")
        set(_debug_crt_arch x64)
    else ()
        message(FATAL_ERROR
                "unsupported Windows processor for Debug CRT staging: ${ZANNA_SYSTEM_PROCESSOR}")
    endif ()
    file(GLOB _debug_crt_dirs LIST_DIRECTORIES TRUE
            "${ZANNA_MSVC_REDIST_DIR}/debug_nonredist/${_debug_crt_arch}/Microsoft.VC*.DebugCRT")
    list(LENGTH _debug_crt_dirs _debug_crt_dir_count)
    if (NOT _debug_crt_dir_count EQUAL 1)
        message(FATAL_ERROR
                "expected one ${_debug_crt_arch} Debug CRT directory under ${ZANNA_MSVC_REDIST_DIR}; found ${_debug_crt_dir_count}")
    endif ()
    list(GET _debug_crt_dirs 0 _debug_crt_dir)
    file(GLOB _debug_crt_files LIST_DIRECTORIES FALSE "${_debug_crt_dir}/*.dll")
    if (NOT _debug_crt_files)
        message(FATAL_ERROR "Debug CRT directory contains no DLLs: ${_debug_crt_dir}")
    endif ()
    file(COPY ${_debug_crt_files} DESTINATION "${_stage_dir}/bin")
endif ()

file(GLOB_RECURSE _staged_sample_files LIST_DIRECTORIES FALSE
        "${_stage_dir}/share/zanna/samples/*")
file(GLOB_RECURSE _staged_vscode_files LIST_DIRECTORIES FALSE
        "${_stage_dir}/share/zanna/vscode/*")
file(GLOB _staged_vsix_files LIST_DIRECTORIES FALSE
        "${_stage_dir}/share/zanna/vscode/*.vsix")
file(GLOB _staged_runtime_files LIST_DIRECTORIES FALSE
        "${_stage_dir}/bin/msvcp*.dll"
        "${_stage_dir}/bin/vcruntime*.dll"
        "${_stage_dir}/bin/concrt*.dll")
if (NOT EXISTS "${_stage_dir}/bin/zannastudio.exe")
    message(FATAL_ERROR "staged Windows toolchain omitted the Zanna Studio component")
endif ()
if (NOT _staged_sample_files)
    message(FATAL_ERROR "staged Windows toolchain omitted the samples component")
endif ()
if (NOT _staged_vscode_files)
    message(FATAL_ERROR "staged Windows toolchain omitted the VS Code component")
endif ()
if (NOT _staged_runtime_files)
    message(FATAL_ERROR "staged Windows toolchain omitted its app-local MSVC runtime closure")
endif ()
foreach (_sample_file IN LISTS _staged_sample_files)
    if (_sample_file MATCHES "\\.(exe|dll|pdb|ilk|lib|exp|obj)$")
        message(FATAL_ERROR "staged samples contain a local build artifact: ${_sample_file}")
    endif ()
endforeach ()
list(GET _staged_sample_files 0 _staged_sample_probe)
list(GET _staged_vscode_files 0 _staged_vscode_probe)
file(RELATIVE_PATH _sample_probe_relative "${_stage_dir}" "${_staged_sample_probe}")
file(RELATIVE_PATH _vscode_probe_relative "${_stage_dir}" "${_staged_vscode_probe}")
set(_installed_sample_probe "${_install_root}/${_sample_probe_relative}")
set(_installed_vscode_probe "${_install_root}/${_vscode_probe_relative}")

# Unsigned/local packages automatically use a development identity that can
# coexist with the stable release unless the packager explicitly overrides it.
execute_process(
        COMMAND "${ZANNA_BIN}" install-package --stage-dir "${_stage_dir}"
                --target windows --stage-only --verbose
        RESULT_VARIABLE _identity_preview_rv
        OUTPUT_VARIABLE _identity_preview_out
        ERROR_VARIABLE _identity_preview_err)
if (NOT _identity_preview_rv EQUAL 0 OR
   NOT _identity_preview_out MATCHES "Windows channel: development" OR
   NOT _identity_preview_out MATCHES "Windows identifier: org.zanna.toolchain.development" OR
   NOT _identity_preview_out MATCHES "Windows install directory: Zanna development" OR
   NOT _identity_preview_out MATCHES "Windows display name: Zanna Toolchain \\(development\\)")
    message(FATAL_ERROR
            "local Windows package identity is not collision-safe\nstdout:\n${_identity_preview_out}\nstderr:\n${_identity_preview_err}")
endif ()

set(_debug_toolchain_args)
if (ZANNA_CONFIG STREQUAL "Debug")
    # This lifecycle test intentionally packages the active local build. Debug
    # CRT payloads are safe here because the installer never leaves the owned
    # test directory and is not presented as a release artifact.
    list(APPEND _debug_toolchain_args --allow-debug-toolchain)
endif ()

set(_package_cmd
        "${ZANNA_BIN}" install-package
        --stage-dir "${_stage_dir}"
        --target windows
        --windows-install-scope user
        --windows-install-dir "${_install_dir_name}"
        --windows-identifier "${_package_identifier}"
        --windows-channel e2e
        --windows-file-associations on
        --windows-shortcuts on
        ${_debug_toolchain_args}
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

set(_repeat_package_cmd ${_package_cmd})
list(POP_BACK _repeat_package_cmd)
list(APPEND _repeat_package_cmd "${_installer_repeat}")
execute_process(
        COMMAND ${_repeat_package_cmd}
        RESULT_VARIABLE _repeat_pkg_rv
        OUTPUT_VARIABLE _repeat_pkg_out
        ERROR_VARIABLE _repeat_pkg_err)
if (NOT _repeat_pkg_rv EQUAL 0)
    message(FATAL_ERROR
            "repeat toolchain installer generation failed\nstdout:\n${_repeat_pkg_out}\nstderr:\n${_repeat_pkg_err}")
endif ()
file(SHA256 "${_installer}" _installer_sha256)
file(SHA256 "${_installer_repeat}" _installer_repeat_sha256)
if (NOT _installer_sha256 STREQUAL _installer_repeat_sha256)
    message(FATAL_ERROR
            "identical staged inputs produced different Windows installers\nfirst:  ${_installer_sha256}\nsecond: ${_installer_repeat_sha256}")
endif ()
execute_process(
        COMMAND "${ZANNA_BIN}" install-package --verify-only "${_installer}"
        RESULT_VARIABLE _verify_rv
        OUTPUT_VARIABLE _verify_out
        ERROR_VARIABLE _verify_err)
if (NOT _verify_rv EQUAL 0)
    message(FATAL_ERROR
            "recursive installer verification failed\nstdout:\n${_verify_out}\nstderr:\n${_verify_err}")
endif ()
set(_inspect_json "${_tmp_root}/inspect.json")
file(WRITE "${_inspect_json}" "stale automation output")
execute_process(
        COMMAND "${_installer}" /inspect /output "${_inspect_json}"
        RESULT_VARIABLE _inspect_rv
        OUTPUT_VARIABLE _inspect_out
        ERROR_VARIABLE _inspect_err)
if (NOT _inspect_rv EQUAL 0 OR NOT EXISTS "${_inspect_json}")
    message(FATAL_ERROR
            "installer inspection did not create its automation output\nstdout:\n${_inspect_out}\nstderr:\n${_inspect_err}")
endif ()
file(READ "${_inspect_json}" _inspect_json_text)
file(GLOB _inspect_temporary_files "${_inspect_json}.tmp-*")
if (_inspect_temporary_files)
    message(FATAL_ERROR "installer inspection left temporary output: ${_inspect_temporary_files}")
endif ()
if (NOT _inspect_json_text MATCHES "\"schema_version\": 3" OR
   NOT _inspect_json_text MATCHES "\"identifier\": \"org.zanna.toolchain.e2e\"" OR
   NOT _inspect_json_text MATCHES "\"display_name\": \"Zanna Toolchain \\(e2e\\)\"" OR
   NOT _inspect_json_text MATCHES "\"channel\": \"e2e\"" OR
   NOT _inspect_json_text MATCHES "\"default_install_dir\": \"ZannaInstallerE2E\"" OR
   NOT _inspect_json_text MATCHES "\"source_commit\": \"[0-9a-f]+\"")
    message(FATAL_ERROR
            "installer inspection did not expose schema/channel/source identity\njson:\n${_inspect_json_text}\nstderr:\n${_inspect_err}")
endif ()
set(_update_json "${_tmp_root}/update.json")
file(WRITE "${_update_json}" "stale automation output")
execute_process(
        COMMAND "${_installer}" /checkForUpdates /quiet /output "${_update_json}"
        RESULT_VARIABLE _update_rv
        OUTPUT_VARIABLE _update_out
        ERROR_VARIABLE _update_err)
if (NOT _update_rv EQUAL 0 OR NOT EXISTS "${_update_json}")
    message(FATAL_ERROR
            "unconfigured update discovery did not create its automation output\nstdout:\n${_update_out}\nstderr:\n${_update_err}")
endif ()
file(READ "${_update_json}" _update_json_text)
file(GLOB _update_temporary_files "${_update_json}.tmp-*")
if (_update_temporary_files)
    message(FATAL_ERROR "update discovery left temporary output: ${_update_temporary_files}")
endif ()
if (NOT _update_json_text MATCHES "\"status\": \"unconfigured\"")
    message(FATAL_ERROR
            "unconfigured update discovery was not deterministic\njson:\n${_update_json_text}\nstderr:\n${_update_err}")
endif ()

execute_process(
        COMMAND "${_installer}" /install /quiet /output "${_tmp_root}/invalid.json"
        RESULT_VARIABLE _invalid_output_rv
        OUTPUT_VARIABLE _invalid_output_out
        ERROR_VARIABLE _invalid_output_err
        TIMEOUT 15)
if (NOT _invalid_output_rv EQUAL 87 OR EXISTS "${_tmp_root}/invalid.json")
    message(FATAL_ERROR
            "installer accepted /output for a mutating operation\nexit: ${_invalid_output_rv}\nstdout:\n${_invalid_output_out}\nstderr:\n${_invalid_output_err}")
endif ()

set(_missing_output_parent "${_tmp_root}/missing-output-parent")
file(REMOVE_RECURSE "${_missing_output_parent}")
set(_missing_output_timeout 15)
if (ZANNA_CONFIG STREQUAL "Debug")
    # A Debug toolchain embeds compiler debug information in its static
    # libraries, so the self-extracting installer can be several hundred MiB.
    # Allow the host to finish extracting before it reports the expected I/O
    # failure; the release payload remains subject to the tighter deadline.
    set(_missing_output_timeout 120)
endif ()
execute_process(
        COMMAND "${_installer}" /inspect /quiet /output "${_missing_output_parent}/inspect.json"
        RESULT_VARIABLE _missing_output_rv
        OUTPUT_VARIABLE _missing_output_out
        ERROR_VARIABLE _missing_output_err
        TIMEOUT ${_missing_output_timeout})
if (NOT _missing_output_rv EQUAL 1603 OR EXISTS "${_missing_output_parent}")
    message(FATAL_ERROR
            "installer did not fail closed for an unwritable automation output\nexit: ${_missing_output_rv}\nstdout:\n${_missing_output_out}\nstderr:\n${_missing_output_err}")
endif ()

if (EXISTS "${_uninstaller}")
    execute_process(COMMAND "${_uninstaller}" /quiet /norestart
            RESULT_VARIABLE _pre_uninstall_rv
            OUTPUT_VARIABLE _pre_uninstall_out
            ERROR_VARIABLE _pre_uninstall_err)
    if (NOT _pre_uninstall_rv EQUAL 0)
        message(FATAL_ERROR
                "pre-existing Zanna uninstall failed\nstdout:\n${_pre_uninstall_out}\nstderr:\n${_pre_uninstall_err}")
    endif ()
endif ()
file(REMOVE_RECURSE "${_install_root}")
file(REMOVE_RECURSE "${_start_menu}")

# A random collision must still be rejected, and the backend reason must survive in a setup log.
file(MAKE_DIRECTORY "${_install_root}/bin")
file(WRITE "${_installed_zanna}" "unowned-collision-sentinel")
file(REMOVE "${_setup_log}")
execute_process(COMMAND "${_installer}" /quiet /norestart
        /installDir "${_install_root}" /type complete /log "${_setup_log}"
        RESULT_VARIABLE _collision_rv
        OUTPUT_VARIABLE _collision_out
        ERROR_VARIABLE _collision_err)
if (_collision_rv EQUAL 0)
    message(FATAL_ERROR "installer replaced an unowned collision")
endif ()
file(READ "${_installed_zanna}" _collision_sentinel)
if (NOT _collision_sentinel STREQUAL "unowned-collision-sentinel")
    message(FATAL_ERROR "failed install modified the unowned collision")
endif ()
if (NOT EXISTS "${_setup_log}")
    message(FATAL_ERROR "failed install did not write its diagnostic log: ${_setup_log}")
endif ()
file(READ "${_setup_log}" _collision_log)
if (NOT _collision_log MATCHES "unowned file conflicts with the new Zanna payload")
    message(FATAL_ERROR
            "setup log omitted the collision reason\nlog:\n${_collision_log}\nstderr:\n${_collision_err}")
endif ()
file(REMOVE_RECURSE "${_install_root}")

execute_process(COMMAND "${_installer}" /quiet /norestart
        /installDir "${_install_root}" /type complete /log "${_setup_log}"
        RESULT_VARIABLE _install_rv
        OUTPUT_VARIABLE _install_out
        ERROR_VARIABLE _install_err)
if (NOT _install_rv EQUAL 0)
    message(FATAL_ERROR
            "per-user Zanna installer failed\nstdout:\n${_install_out}\nstderr:\n${_install_err}")
endif ()

_zanna_installer_assert_exists("${_installed_zannastudio}" "default Zanna Studio component")
_zanna_installer_assert_exists("${_installed_zannastudio_icon}" "default Zanna Studio icon")
_zanna_installer_assert_exists("${_installed_sample_probe}" "default samples component")
_zanna_installer_assert_exists("${_installed_vscode_probe}" "default VS Code component")
if (_staged_vsix_files)
    _zanna_installer_assert_exists("${_installed_vscode_helper}" "default VS Code helper")
endif ()
_zanna_installer_assert_exists("${_developer_prompt_shortcut}" "developer prompt shortcut")
_zanna_installer_assert_exists("${_zannastudio_shortcut}" "default Zanna Studio shortcut")
if (_staged_vsix_files)
    _zanna_installer_assert_exists("${_vscode_shortcut}" "default VS Code shortcut")
endif ()
file(READ "${_installed_manifest}" _default_component_manifest)
string(REPLACE "\\" "/" _default_component_manifest "${_default_component_manifest}")
_zanna_installer_assert_manifest_contains(
        "${_default_component_manifest}" "bin/zannastudio.exe" "default component manifest")
_zanna_installer_assert_manifest_contains(
        "${_default_component_manifest}" "share/zanna/samples/" "default component manifest")
_zanna_installer_assert_manifest_contains(
        "${_default_component_manifest}" "share/zanna/vscode/" "default component manifest")

# A missing/corrupt manifest can be reconstructed only from the fully verified
# maintenance package; arbitrary files in the same directory remain unowned.
if (NOT EXISTS "${_installed_manifest}")
    message(FATAL_ERROR "clean install did not write ${_installed_manifest}")
endif ()
set(_unowned_sentinel "${_install_root}/developer-unowned-sentinel.txt")
file(WRITE "${_unowned_sentinel}" "preserve-me")
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
        /installDir "${_install_root}" /type complete /log "${_setup_log}"
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
_zanna_installer_assert_exists("${_unowned_sentinel}" "unowned file during manifest recovery")
file(READ "${_setup_log}" _legacy_upgrade_log)
if (NOT _legacy_upgrade_log MATCHES "Migrating a verified Zanna installation")
    message(FATAL_ERROR "verified migration was not recorded in the setup log:\n${_legacy_upgrade_log}")
endif ()

# Component opt-outs are upgrade-safe: stale owned files and shortcuts are removed while core
# tools and unrelated developer files remain untouched.
execute_process(
        COMMAND "${_installer}" /modify /quiet /norestart /type minimal
                /noPath /noAssociations /noShortcuts /log "${_setup_log}"
        RESULT_VARIABLE _minimal_upgrade_rv
        OUTPUT_VARIABLE _minimal_upgrade_out
        ERROR_VARIABLE _minimal_upgrade_err)
if (NOT _minimal_upgrade_rv EQUAL 0)
    message(FATAL_ERROR
            "component opt-out upgrade failed\nstdout:\n${_minimal_upgrade_out}\nstderr:\n${_minimal_upgrade_err}")
endif ()
_zanna_installer_assert_exists("${_installed_zanna}" "core tool after component opt-out")
_zanna_installer_assert_exists("${_developer_prompt}" "developer prompt after component opt-out")
_zanna_installer_assert_exists("${_unowned_sentinel}" "unowned developer file")
_zanna_installer_assert_absent("${_installed_zannastudio}" "opted-out Zanna Studio executable")
_zanna_installer_assert_absent("${_installed_zannastudio_icon}" "opted-out Zanna Studio icon")
_zanna_installer_assert_absent("${_installed_sample_probe}" "opted-out sample")
_zanna_installer_assert_absent("${_installed_vscode_probe}" "opted-out VS Code payload")
_zanna_installer_assert_absent("${_installed_vscode_helper}" "opted-out VS Code helper")
_zanna_installer_assert_absent("${_zannastudio_shortcut}" "opted-out Zanna Studio shortcut")
_zanna_installer_assert_absent(
        "${_developer_prompt_shortcut}" "disabled developer prompt shortcut")
_zanna_installer_assert_absent("${_start_menu}" "disabled Start Menu folder")
if (_staged_vsix_files)
    _zanna_installer_assert_absent("${_vscode_shortcut}" "opted-out VS Code shortcut")
endif ()
file(READ "${_installed_manifest}" _minimal_component_manifest)
string(REPLACE "\\" "/" _minimal_component_manifest "${_minimal_component_manifest}")
_zanna_installer_assert_manifest_excludes(
        "${_minimal_component_manifest}" "bin/zannastudio.exe" "minimal component manifest")
_zanna_installer_assert_manifest_excludes(
        "${_minimal_component_manifest}" "share/zanna/samples/" "minimal component manifest")
_zanna_installer_assert_manifest_excludes(
        "${_minimal_component_manifest}" "share/zanna/vscode/" "minimal component manifest")
_zanna_installer_assert_manifest_excludes(
        "${_minimal_component_manifest}"
        "bin/zanna-install-vscode-extension.cmd"
        "minimal component manifest")

# Complete Modify restores every packaged component and developer integration.
execute_process(COMMAND "${_installer}" /modify /quiet /norestart /type complete
        /addToPath /associations /shortcuts /log "${_setup_log}"
        RESULT_VARIABLE _restore_upgrade_rv
        OUTPUT_VARIABLE _restore_upgrade_out
        ERROR_VARIABLE _restore_upgrade_err)
if (NOT _restore_upgrade_rv EQUAL 0)
    message(FATAL_ERROR
            "default component restore failed\nstdout:\n${_restore_upgrade_out}\nstderr:\n${_restore_upgrade_err}")
endif ()
_zanna_installer_assert_exists("${_installed_zannastudio}" "restored Zanna Studio component")
_zanna_installer_assert_exists("${_installed_zannastudio_icon}" "restored Zanna Studio icon")
_zanna_installer_assert_exists("${_installed_sample_probe}" "restored samples component")
_zanna_installer_assert_exists("${_installed_vscode_probe}" "restored VS Code component")
_zanna_installer_assert_exists("${_zannastudio_shortcut}" "restored Zanna Studio shortcut")
if (_staged_vsix_files)
    _zanna_installer_assert_exists("${_installed_vscode_helper}" "restored VS Code helper")
    _zanna_installer_assert_exists("${_vscode_shortcut}" "restored VS Code shortcut")
endif ()
foreach (_runtime_file IN LISTS _staged_runtime_files)
    get_filename_component(_runtime_name "${_runtime_file}" NAME)
    _zanna_installer_assert_exists(
            "${_install_root}/bin/${_runtime_name}" "app-local runtime ${_runtime_name}")
endforeach ()
_zanna_installer_assert_absent(
        "${_install_root}/bin/zanna-installer-host.exe" "installer-only native host")
_zanna_installer_assert_absent(
        "${_install_root}/bin/zanna-installer-cleanup.exe" "installer-only cleanup helper")
file(READ "${_installed_manifest}" _restored_component_manifest)
string(REPLACE "\\" "/" _restored_component_manifest "${_restored_component_manifest}")
_zanna_installer_assert_manifest_contains(
        "${_restored_component_manifest}" "bin/zannastudio.exe" "restored component manifest")
_zanna_installer_assert_manifest_contains(
        "${_restored_component_manifest}" "share/zanna/samples/" "restored component manifest")
_zanna_installer_assert_manifest_contains(
        "${_restored_component_manifest}" "share/zanna/vscode/" "restored component manifest")

file(TO_NATIVE_PATH "${_install_root}" _install_root_native)
file(TO_NATIVE_PATH "${_zannastudio_shortcut}" _zannastudio_shortcut_native)
file(TO_NATIVE_PATH "${_developer_prompt_shortcut}" _developer_prompt_shortcut_native)
set(_integration_probe "${_tmp_root}/probe-integrations.ps1")
file(WRITE "${_integration_probe}"
        "${_utf8_bom}"
        "$ErrorActionPreference = 'Stop'\n"
        "$root = '${_install_root_native}'\n"
        "$identifier = '${_package_identifier}'\n"
        "$bin = [IO.Path]::Combine($root, 'bin')\n"
        "$entries = @([Environment]::GetEnvironmentVariable('Path', 'User') -split ';' | Where-Object { $_ })\n"
        "$owned = @($entries | Where-Object { [string]::Equals($_.TrimEnd('\\'), $bin.TrimEnd('\\'), [StringComparison]::OrdinalIgnoreCase) })\n"
        "if ($owned.Count -ne 1) { throw \"Expected exactly one owned PATH entry; found $($owned.Count)\" }\n"
        "$expectedCommand = '\"' + [IO.Path]::Combine($root, 'bin\\zannastudio.exe') + '\" \"%1\"'\n"
        "foreach ($extension in @('zia', 'bas', 'il')) {\n"
        "  $progId = \"$identifier.$extension\"\n"
        "  $commandKey = \"Registry::HKEY_CURRENT_USER\\Software\\Classes\\$progId\\shell\\open\\command\"\n"
        "  if (-not (Test-Path -LiteralPath $commandKey)) { throw \"Missing safe association command $commandKey\" }\n"
        "  $command = (Get-Item -LiteralPath $commandKey).GetValue('')\n"
        "  if ($command -cne $expectedCommand) { throw \"Unsafe association command: $command\" }\n"
        "  $openWith = \"Registry::HKEY_CURRENT_USER\\Software\\Classes\\.$extension\\OpenWithProgids\"\n"
        "  if (-not (Test-Path -LiteralPath $openWith) -or (Get-Item -LiteralPath $openWith).GetValueNames() -notcontains $progId) { throw \"Missing OpenWith registration for .$extension\" }\n"
        "}\n"
        "$arpPath = \"Registry::HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$identifier\"\n"
        "$arp = Get-ItemProperty -LiteralPath $arpPath\n"
        "foreach ($name in @('DisplayName','DisplayVersion','Publisher','UninstallString','QuietUninstallString','ModifyPath','RepairPath','DisplayIcon','ZannaArchitecture','ZannaChannel','ZannaCommit','ZannaPackageSha256','ZannaComponents','ZannaMaintenanceCache','ZannaLastInstallerLog')) { if (-not $arp.PSObject.Properties[$name] -or [string]::IsNullOrWhiteSpace([string]$arp.$name)) { throw \"ARP field is missing: $name\" } }\n"
        "if ($arp.ZannaChannel -cne 'e2e') { throw \"Wrong ARP channel: $($arp.ZannaChannel)\" }\n"
        "if ($arp.ZannaPackageSha256 -cne '${_installer_sha256}') { throw \"Wrong ARP package digest\" }\n"
        "if (-not (Test-Path -LiteralPath $arp.ZannaMaintenanceCache -PathType Leaf)) { throw \"Maintenance cache is missing\" }\n"
        "$shell = New-Object -ComObject Shell.Application\n"
        "function Get-ShellLink([string]$path) { $folder = $shell.Namespace((Split-Path -Parent $path)); $item = $folder.ParseName((Split-Path -Leaf $path)); if (-not $item) { throw \"Cannot inspect shortcut $path\" }; return $item.GetLink }\n"
        "$ide = Get-ShellLink '${_zannastudio_shortcut_native}'\n"
        "if (-not [string]::Equals($ide.Path, [IO.Path]::Combine($root, 'bin\\zannastudio.exe'), [StringComparison]::OrdinalIgnoreCase)) { throw \"Zanna Studio shortcut target is not destination-aware: $($ide.Path)\" }\n"
        "$prompt = Get-ShellLink '${_developer_prompt_shortcut_native}'\n"
        "$expectedPromptArguments = '/k \"' + [IO.Path]::Combine($root, 'bin\\zanna-dev.cmd') + '\"'\n"
        "if ($prompt.Arguments -cne $expectedPromptArguments) { throw \"Developer shortcut arguments are not destination-aware: $($prompt.Arguments)\" }\n"
        "Write-Output $arp.ZannaMaintenanceCache\n")
execute_process(
        COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
                -File "${_integration_probe}"
        RESULT_VARIABLE _integration_rv
        OUTPUT_VARIABLE _maintenance_cache
        ERROR_VARIABLE _integration_err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT _integration_rv EQUAL 0)
    message(FATAL_ERROR "Windows integration probe failed\n${_integration_err}")
endif ()
file(TO_CMAKE_PATH "${_maintenance_cache}" _maintenance_cache)

# Repair must restore damaged owned bytes without touching developer content.
file(SHA256 "${_installed_zanna}" _installed_zanna_sha256)
file(APPEND "${_installed_zanna}" "corrupt-owned-bytes")
execute_process(
        COMMAND "${_installer}" /repair /quiet /norestart /log "${_setup_log}"
        RESULT_VARIABLE _repair_rv
        OUTPUT_VARIABLE _repair_out
        ERROR_VARIABLE _repair_err)
if (NOT _repair_rv EQUAL 0)
    message(FATAL_ERROR
            "repair failed\nstdout:\n${_repair_out}\nstderr:\n${_repair_err}")
endif ()
file(SHA256 "${_installed_zanna}" _repaired_zanna_sha256)
if (NOT _repaired_zanna_sha256 STREQUAL _installed_zanna_sha256)
    message(FATAL_ERROR "repair did not restore the exact owned zanna.exe bytes")
endif ()
_zanna_installer_assert_exists("${_unowned_sentinel}" "unowned file after repair")

# The package mutex must deterministically reject one of two concurrent repairs.
file(TO_NATIVE_PATH "${_installer}" _installer_native)
set(_concurrency_probe "${_tmp_root}/probe-concurrency.ps1")
file(WRITE "${_concurrency_probe}"
        "${_utf8_bom}"
        "$ErrorActionPreference = 'Stop'\n"
        "$first = Start-Process -FilePath '${_installer_native}' -ArgumentList @('/repair','/quiet','/norestart','/log','${_tmp_root}\\concurrent-first.log') -PassThru\n"
        "Start-Sleep -Milliseconds 100\n"
        "$second = Start-Process -FilePath '${_installer_native}' -ArgumentList @('/repair','/quiet','/norestart','/log','${_tmp_root}\\concurrent-second.log') -PassThru\n"
        "$first.WaitForExit()\n"
        "$second.WaitForExit()\n"
        "$codes = @($first.ExitCode, $second.ExitCode) | Sort-Object\n"
        "if ($codes.Count -ne 2 -or $codes[0] -ne 0 -or $codes[1] -ne 1618) { throw \"Expected concurrent exit codes 0 and 1618; got $($first.ExitCode), $($second.ExitCode)\" }\n")
execute_process(
        COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
                -File "${_concurrency_probe}"
        RESULT_VARIABLE _concurrency_rv
        ERROR_VARIABLE _concurrency_err)
if (NOT _concurrency_rv EQUAL 0)
    message(FATAL_ERROR "concurrent installer probe failed\n${_concurrency_err}")
endif ()

# Restart Manager must refuse a silent destructive update, then close an eligible
# Zanna Studio process only when the caller explicitly authorizes it.
file(TO_NATIVE_PATH "${_installed_zannastudio}" _installed_zannastudio_native)
set(_restart_manager_probe "${_tmp_root}/probe-restart-manager.ps1")
file(WRITE "${_restart_manager_probe}"
        "${_utf8_bom}"
        "$ErrorActionPreference = 'Stop'\n"
        "$ide = Start-Process -FilePath '${_installed_zannastudio_native}' -PassThru\n"
        "try {\n"
        "  Start-Sleep -Seconds 2\n"
        "  if ($ide.HasExited) { throw \"Zanna Studio exited before the files-in-use probe\" }\n"
        "  $blocked = Start-Process -FilePath '${_installer_native}' -ArgumentList @('/repair','/quiet','/norestart','/log','${_tmp_root}\\files-in-use-blocked.log') -PassThru -Wait\n"
        "  if ($blocked.ExitCode -ne 1603) { throw \"Repair without /closeApplications returned $($blocked.ExitCode), expected 1603\" }\n"
        "  if ($ide.HasExited) { throw \"Installer closed Zanna Studio without authorization\" }\n"
        "  $closed = Start-Process -FilePath '${_installer_native}' -ArgumentList @('/repair','/quiet','/norestart','/closeApplications','/log','${_tmp_root}\\files-in-use-closed.log') -PassThru -Wait\n"
        "  if ($closed.ExitCode -ne 0) { throw \"Authorized files-in-use repair returned $($closed.ExitCode)\" }\n"
        "  $ide.WaitForExit(10000) | Out-Null\n"
        "  if (-not $ide.HasExited) { throw \"Restart Manager did not close Zanna Studio\" }\n"
        "} finally { if (-not $ide.HasExited) { Stop-Process -Id $ide.Id -Force -ErrorAction SilentlyContinue } }\n")
execute_process(
        COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
                -File "${_restart_manager_probe}"
        RESULT_VARIABLE _restart_manager_rv
        ERROR_VARIABLE _restart_manager_err)
if (NOT _restart_manager_rv EQUAL 0)
    message(FATAL_ERROR "Restart Manager probe failed\n${_restart_manager_err}")
endif ()

zanna_installer_smoke_verify_installed_tools("${_install_root}/bin" ".exe" "Windows installer E2E")

set(_path_probe_ps [=[$machine=[Environment]::GetEnvironmentVariable('Path','Machine'); $user=[Environment]::GetEnvironmentVariable('Path','User'); $env:Path=($machine + ';' + $user); zanna --version]=])
execute_process(COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -Command "${_path_probe_ps}"
        RESULT_VARIABLE _path_rv
        OUTPUT_VARIABLE _path_out
        ERROR_VARIABLE _path_err)
if (NOT _path_rv EQUAL 0)
    message(FATAL_ERROR
            "installed zanna was not discoverable through a fresh registry PATH projection\nstdout:\n${_path_out}\nstderr:\n${_path_err}")
endif ()

set(_run_bas "${_tmp_root}/installer-run-smoke.bas")
file(WRITE "${_run_bas}" "10 PRINT \"INSTALLER-RUN-SMOKE\"\n")
execute_process(COMMAND "${_installed_zanna}" run "${_run_bas}"
        RESULT_VARIABLE _run_rv
        OUTPUT_VARIABLE _run_out
        ERROR_VARIABLE _run_err)
if (NOT _run_rv EQUAL 0)
    message(FATAL_ERROR
            "installed zanna run failed\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
endif ()
if (NOT _run_out MATCHES "INSTALLER-RUN-SMOKE")
    message(FATAL_ERROR
            "installed zanna run produced unexpected output\nstdout:\n${_run_out}\nstderr:\n${_run_err}")
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

extern @Zanna.Terminal.PrintStr(str) -> void
global const str @.msg = "INSTALLER-NATIVE-SMOKE"

func @main() -> i64 {
entry:
  %msg = const_str @.msg
  call @Zanna.Terminal.PrintStr(%msg)
  ret 0
}
]=])
execute_process(
        COMMAND "${CMAKE_BIN}" -E env --unset=ZANNA_LIB_PATH "${_installed_zanna}" codegen "${_installed_codegen_arch}" "${_installed_il}" -o "${_installed_exe}"
        WORKING_DIRECTORY "${_tmp_root}"
        RESULT_VARIABLE _codegen_rv
        OUTPUT_VARIABLE _codegen_out
        ERROR_VARIABLE _codegen_err)
if (NOT _codegen_rv EQUAL 0)
    message(FATAL_ERROR
            "installed zanna native codegen failed\nstdout:\n${_codegen_out}\nstderr:\n${_codegen_err}")
endif ()
if (NOT EXISTS "${_installed_exe}")
    message(FATAL_ERROR "installed zanna did not produce native smoke executable: ${_installed_exe}")
endif ()
execute_process(COMMAND "${_installed_exe}"
        WORKING_DIRECTORY "${_tmp_root}"
        RESULT_VARIABLE _native_rv
        OUTPUT_VARIABLE _native_out
        ERROR_VARIABLE _native_err)
if (NOT _native_rv EQUAL 0)
    message(FATAL_ERROR
            "native executable built by installed zanna failed\nstdout:\n${_native_out}\nstderr:\n${_native_err}")
endif ()
if (NOT _native_out MATCHES "INSTALLER-NATIVE-SMOKE")
    message(FATAL_ERROR
            "native executable built by installed zanna produced unexpected output\nstdout:\n${_native_out}\nstderr:\n${_native_err}")
endif ()

file(WRITE "${_src_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(zanna_installer_e2e_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Zanna CONFIG REQUIRED)
add_executable(zanna_installer_e2e_consumer main.cpp)
target_link_libraries(zanna_installer_e2e_consumer PRIVATE zanna::il_core zanna::il_io)
]=])

file(WRITE "${_src_dir}/main.cpp" [=[
#include <sstream>
#include <zanna/il/core/Module.hpp>
#include <zanna/il/io/Serializer.hpp>

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
        "chcp 65001 >nul\r\n"
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
            "external find_package(Zanna) configure failed against installed toolchain\nstdout:\n${_cfg_out}\nstderr:\n${_cfg_err}")
endif ()

set(_consumer_build_cmd "${CMAKE_BIN}" --build "${_build_dir}")
if (DEFINED ZANNA_CONFIG AND NOT "${ZANNA_CONFIG}" STREQUAL "")
    list(APPEND _consumer_build_cmd --config "${ZANNA_CONFIG}")
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
file(REMOVE "${_unowned_sentinel}")
execute_process(COMMAND "${_uninstaller}" /quiet /norestart
        RESULT_VARIABLE _uninstall_rv
        OUTPUT_VARIABLE _uninstall_out
        ERROR_VARIABLE _uninstall_err)
if (NOT _uninstall_rv EQUAL 0)
    message(FATAL_ERROR
            "per-user Zanna uninstall failed\nstdout:\n${_uninstall_out}\nstderr:\n${_uninstall_err}")
endif ()
file(TO_NATIVE_PATH "${_maintenance_cache}" _maintenance_cache_native)
file(TO_NATIVE_PATH "${_start_menu}" _start_menu_native)
set(_uninstall_probe "${_tmp_root}/probe-uninstall-cleanup.ps1")
file(WRITE "${_uninstall_probe}"
        "${_utf8_bom}"
        "$ErrorActionPreference = 'Stop'\n"
        "$deadline = [DateTime]::UtcNow.AddSeconds(45)\n"
        "$arp = 'Registry::HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${_package_identifier}'\n"
        "while ([DateTime]::UtcNow -lt $deadline -and ((Test-Path -LiteralPath '${_install_root_native}') -or (Test-Path -LiteralPath '${_maintenance_cache_native}') -or (Test-Path -LiteralPath $arp))) { Start-Sleep -Milliseconds 100 }\n"
        "if (Test-Path -LiteralPath '${_install_root_native}') { throw 'Uninstall left the installation root' }\n"
        "if (Test-Path -LiteralPath '${_maintenance_cache_native}') { throw 'Uninstall left the maintenance cache executable' }\n"
        "if (Test-Path -LiteralPath (Split-Path -Parent '${_maintenance_cache_native}')) { throw 'Uninstall left the package cache directory' }\n"
        "if (Test-Path -LiteralPath $arp) { throw 'Uninstall left Add/Remove Programs metadata' }\n"
        "if (Test-Path -LiteralPath '${_start_menu_native}') { throw 'Uninstall left the Start Menu product folder' }\n"
        "$entries = @([Environment]::GetEnvironmentVariable('Path', 'User') -split ';' | Where-Object { $_ })\n"
        "if ($entries | Where-Object { [string]::Equals($_.TrimEnd('\\'), [IO.Path]::Combine('${_install_root_native}', 'bin').TrimEnd('\\'), [StringComparison]::OrdinalIgnoreCase) }) { throw 'Uninstall left its PATH entry' }\n"
        "foreach ($extension in @('zia','bas','il')) {\n"
        "  $progId = '${_package_identifier}.' + $extension\n"
        "  if (Test-Path -LiteralPath (\"Registry::HKEY_CURRENT_USER\\Software\\Classes\\$progId\")) { throw \"Uninstall left ProgID $progId\" }\n"
        "  $openWith = \"Registry::HKEY_CURRENT_USER\\Software\\Classes\\.$extension\\OpenWithProgids\"\n"
        "  if ((Test-Path -LiteralPath $openWith) -and ((Get-Item -LiteralPath $openWith).GetValueNames() -contains $progId)) { throw \"Uninstall left OpenWith value $progId\" }\n"
        "}\n")
execute_process(
        COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
                -File "${_uninstall_probe}"
        RESULT_VARIABLE _uninstall_probe_rv
        ERROR_VARIABLE _uninstall_probe_err)
if (NOT _uninstall_probe_rv EQUAL 0)
    message(FATAL_ERROR "residue-free uninstall probe failed\n${_uninstall_probe_err}")
endif ()
_zanna_installer_assert_absent("${_zannastudio_shortcut}" "uninstalled Zanna Studio shortcut")
_zanna_installer_assert_absent(
        "${_developer_prompt_shortcut}" "uninstalled developer prompt shortcut")
if (_staged_vsix_files)
    _zanna_installer_assert_absent("${_vscode_shortcut}" "uninstalled VS Code shortcut")
endif ()

if (ZANNA_INSTALLER_TEST_HOOKS)
    function (_zanna_run_installer_hook environment_name environment_value expected_exit)
        execute_process(
                COMMAND "${CMAKE_BIN}" -E env
                        "${environment_name}=${environment_value}"
                        "${_installer}" ${ARGN}
                RESULT_VARIABLE _hook_rv
                OUTPUT_VARIABLE _hook_out
                ERROR_VARIABLE _hook_err)
        if (NOT _hook_rv EQUAL expected_exit)
            message(FATAL_ERROR
                    "installer hook ${environment_name}=${environment_value} returned ${_hook_rv}, expected ${expected_exit}\nstdout:\n${_hook_out}\nstderr:\n${_hook_err}")
        endif ()
    endfunction ()

    function (_zanna_assert_no_transaction_residue cache_executable context)
        get_filename_component(_cache_dir "${cache_executable}" DIRECTORY)
        if (EXISTS "${_cache_dir}/recovery-v2.txt")
            message(FATAL_ERROR "${context} left a recovery marker")
        endif ()
        get_filename_component(_install_parent "${_install_root}" DIRECTORY)
        get_filename_component(_install_leaf "${_install_root}" NAME)
        file(GLOB _transactions LIST_DIRECTORIES TRUE
                "${_install_parent}/.${_install_leaf}.zanna-transaction-*")
        if (_transactions)
            message(FATAL_ERROR "${context} left transaction directories: ${_transactions}")
        endif ()
    endfunction ()

    set(_fault_log "${_tmp_root}/fault-injection.log")

    # An unsupported Windows release is rejected before any installation state
    # is created. The override exists only in this fault-enabled host build.
    _zanna_run_installer_hook(
            ZANNA_INSTALLER_TEST_WINDOWS_VERSION 6.3.9600 1603
            /install /quiet /norestart /installDir "${_install_root}" /type minimal
            /log "${_fault_log}")
    _zanna_installer_assert_absent("${_install_root}" "Windows-version-preflight install root")

    # Disk exhaustion is rejected before the first filesystem or registry mutation.
    _zanna_run_installer_hook(
            ZANNA_INSTALLER_TEST_FREE_BYTES 1 1603
            /install /quiet /norestart /installDir "${_install_root}" /type minimal
            /log "${_fault_log}")
    _zanna_installer_assert_absent("${_install_root}" "disk-preflight install root")

    # Cooperative cancellation after verified staging rolls back as exit 1602.
    _zanna_run_installer_hook(
            ZANNA_INSTALLER_TEST_CANCEL_AT after-stage 1602
            /install /quiet /norestart /installDir "${_install_root}" /type minimal
            /log "${_fault_log}")
    _zanna_installer_assert_absent("${_install_root}" "cancelled install root")

    execute_process(
            COMMAND "${_installer}" /install /quiet /norestart
                    /installDir "${_install_root}" /type minimal /noPath /noAssociations
                    /noShortcuts /log "${_fault_log}"
            RESULT_VARIABLE _fault_baseline_rv
            OUTPUT_VARIABLE _fault_baseline_out
            ERROR_VARIABLE _fault_baseline_err)
    if (NOT _fault_baseline_rv EQUAL 0)
        message(FATAL_ERROR
                "fault-test baseline install failed\n${_fault_baseline_out}\n${_fault_baseline_err}")
    endif ()
    file(SHA256 "${_installed_zanna}" _fault_baseline_sha256)
    execute_process(
            COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
                    -Command "(Get-ItemProperty -LiteralPath 'Registry::HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${_package_identifier}').ZannaMaintenanceCache"
            RESULT_VARIABLE _fault_cache_rv
            OUTPUT_VARIABLE _fault_cache
            ERROR_VARIABLE _fault_cache_err
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT _fault_cache_rv EQUAL 0 OR _fault_cache STREQUAL "")
        message(FATAL_ERROR "cannot locate fault-test maintenance cache\n${_fault_cache_err}")
    endif ()
    file(TO_CMAKE_PATH "${_fault_cache}" _fault_cache)

    # A synchronous registry-phase failure must immediately restore the prior tree and metadata.
    _zanna_run_installer_hook(
            ZANNA_INSTALLER_TEST_FAIL_AT after-registry 1603
            /repair /quiet /norestart /log "${_fault_log}")
    file(SHA256 "${_installed_zanna}" _fault_recovered_sha256)
    if (NOT _fault_recovered_sha256 STREQUAL _fault_baseline_sha256)
        message(FATAL_ERROR "synchronous fault rollback changed the installed zanna.exe")
    endif ()
    _zanna_assert_no_transaction_residue("${_fault_cache}" "synchronous fault rollback")

    # Hard termination at each durable directory/metadata state is recovered by
    # the next invocation before it starts a fresh repair transaction.
    foreach (_crash_stage after-old-move after-new-move after-registry)
        _zanna_run_installer_hook(
                ZANNA_INSTALLER_TEST_CRASH_AT "${_crash_stage}" 1603
                /repair /quiet /norestart /log "${_fault_log}")
        get_filename_component(_fault_cache_dir "${_fault_cache}" DIRECTORY)
        _zanna_installer_assert_exists(
                "${_fault_cache_dir}/recovery-v2.txt" "${_crash_stage} recovery marker")
        execute_process(
                COMMAND "${_installer}" /repair /quiet /norestart /log "${_fault_log}"
                RESULT_VARIABLE _crash_recovery_rv
                OUTPUT_VARIABLE _crash_recovery_out
                ERROR_VARIABLE _crash_recovery_err)
        if (NOT _crash_recovery_rv EQUAL 0)
            message(FATAL_ERROR
                    "recovery after ${_crash_stage} failed\n${_crash_recovery_out}\n${_crash_recovery_err}")
        endif ()
        file(SHA256 "${_installed_zanna}" _crash_recovered_sha256)
        if (NOT _crash_recovered_sha256 STREQUAL _fault_baseline_sha256)
            message(FATAL_ERROR "recovery after ${_crash_stage} changed zanna.exe")
        endif ()
        _zanna_assert_no_transaction_residue(
                "${_fault_cache}" "recovery after ${_crash_stage}")
    endforeach ()

    # Interrupted uninstall states converge to complete removal. Reinstall a
    # minimal baseline between stages so every durable state is exercised.
    foreach (_crash_stage after-old-move after-new-move after-registry)
        _zanna_run_installer_hook(
                ZANNA_INSTALLER_TEST_CRASH_AT "${_crash_stage}" 1603
                /uninstall /quiet /norestart /scope user /log "${_fault_log}")
        execute_process(
                COMMAND "${_installer}" /uninstall /quiet /norestart /scope user
                        /log "${_fault_log}"
                RESULT_VARIABLE _uninstall_recovery_rv
                OUTPUT_VARIABLE _uninstall_recovery_out
                ERROR_VARIABLE _uninstall_recovery_err)
        if (NOT _uninstall_recovery_rv EQUAL 0)
            message(FATAL_ERROR
                    "uninstall recovery after ${_crash_stage} failed\n${_uninstall_recovery_out}\n${_uninstall_recovery_err}")
        endif ()
        file(TO_NATIVE_PATH "${_fault_cache}" _fault_cache_native)
        execute_process(
                COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
                        -Command "$deadline=[DateTime]::UtcNow.AddSeconds(30); while([DateTime]::UtcNow -lt $deadline -and ((Test-Path -LiteralPath '${_install_root_native}') -or (Test-Path -LiteralPath '${_fault_cache_native}'))){Start-Sleep -Milliseconds 100}; if((Test-Path -LiteralPath '${_install_root_native}') -or (Test-Path -LiteralPath '${_fault_cache_native}')){exit 1}"
                RESULT_VARIABLE _cleanup_wait_rv)
        if (NOT _cleanup_wait_rv EQUAL 0)
            message(FATAL_ERROR "uninstall recovery after ${_crash_stage} left owned residue")
        endif ()
        if (NOT _crash_stage STREQUAL "after-registry")
            execute_process(
                    COMMAND "${_installer}" /install /quiet /norestart
                            /installDir "${_install_root}" /type minimal /noPath
                            /noAssociations /noShortcuts /log "${_fault_log}"
                    RESULT_VARIABLE _fault_reinstall_rv
                    OUTPUT_VARIABLE _fault_reinstall_out
                    ERROR_VARIABLE _fault_reinstall_err)
            if (NOT _fault_reinstall_rv EQUAL 0)
                message(FATAL_ERROR
                        "fault-test reinstall failed\n${_fault_reinstall_out}\n${_fault_reinstall_err}")
            endif ()
        endif ()
    endforeach ()
endif ()
