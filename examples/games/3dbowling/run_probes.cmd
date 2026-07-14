@echo off
rem ===----------------------------------------------------------------------===
rem
rem Part of the Viper project, under the GNU GPL v3.
rem See LICENSE for license information.
rem
rem ===----------------------------------------------------------------------===
rem
rem File: examples/games/3dbowling/run_probes.cmd
rem Purpose: Run every 3dbowling release gate with an existing Viper binary.
rem Key invariants:
rem   - The script never configures, builds, or invokes CTest.
rem   - A probe passes only when it exits cleanly and prints exactly RESULT: ok.
rem Ownership/Lifetime:
rem   - Temporary output is removed after every probe and on normal exit.
rem Links: run_probes.sh, LONG_TERM_IMPROVEMENT_SPEC.md
rem
rem ===----------------------------------------------------------------------===

setlocal EnableExtensions EnableDelayedExpansion
set "DEMO_DIR=%~dp0"
for %%I in ("%DEMO_DIR%\..\..\..") do set "REPO_ROOT=%%~fI"
if not defined VIPER_BIN set "VIPER_BIN=viper"
set "OUTPUT=%TEMP%\3dbowling-probe-%RANDOM%-%RANDOM%.txt"
set /a PASSED=0
set /a FAILED=0

where "%VIPER_BIN%" >nul 2>nul
if errorlevel 1 if not exist "%VIPER_BIN%" (
    echo 3dbowling probes: Viper binary not found: %VIPER_BIN% 1>&2
    echo Set VIPER_BIN to an existing viper executable; this runner never builds it. 1>&2
    exit /b 1
)

pushd "%REPO_ROOT%" || exit /b 1
for %%P in (
    release_upgrade_probe pinfall_contract_probe impact_order_probe
    oil_grid_probe trajectory_probe ai_delivery_probe feedback_probe
    stability_probe replay_scene_probe lifecycle_probe asset_resolution_probe
    save_clamp_probe frame_rate_probe layout_probe accessibility_probe
    menu_flow_probe match_mode_probe asset_probe asset_render_probe
    aim_smoke_probe overlay_smoke_probe smoke_probe title_nopostfx_smoke
    title_postfx_smoke scene_nopostfx_smoke
    release_visual_probe release_menu_probe
) do call :run_probe %%P
popd
del /q "%OUTPUT%" >nul 2>nul
echo 3dbowling probes: !PASSED! passed, !FAILED! failed
if not "!FAILED!"=="0" exit /b 1
exit /b 0

:run_probe
echo ==^> %1
"%VIPER_BIN%" run "%DEMO_DIR%%1.zia" >"%OUTPUT%" 2>&1
set "PROBE_STATUS=%ERRORLEVEL%"
type "%OUTPUT%"
if not "%PROBE_STATUS%"=="0" goto :probe_failed
findstr /x /c:"RESULT: ok" "%OUTPUT%" >nul
if errorlevel 1 goto :probe_failed
set /a PASSED+=1
goto :eof

:probe_failed
echo PROBE FAILED: %1 ^(exit %PROBE_STATUS%^) 1>&2
set /a FAILED+=1
goto :eof
