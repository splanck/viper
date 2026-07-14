@echo off
rem ===----------------------------------------------------------------------===
rem
rem Part of the Viper project, under the GNU GPL v3.
rem See LICENSE for license information.
rem
rem ===----------------------------------------------------------------------===
rem
rem File: examples/games/ridgebound/run_probes.cmd
rem Purpose: Run every Ridgebound release gate with an existing Viper binary.
rem Key invariants:
rem   - The script never configures, builds, or invokes CTest.
rem   - A probe passes only after a clean exit and a RESULT: ok line.
rem Ownership/Lifetime:
rem   - Temporary output is removed after every probe and on normal exit.
rem Links: run_probes.sh, IMPROVEMENT_AUDIT.md
rem
rem ===----------------------------------------------------------------------===

setlocal EnableExtensions EnableDelayedExpansion
set "DEMO_DIR=%~dp0"
for %%I in ("%DEMO_DIR%\..\..\..") do set "REPO_ROOT=%%~fI"
if not defined VIPER_BIN set "VIPER_BIN=viper"
set "OUTPUT=%TEMP%\ridgebound-probe-%RANDOM%-%RANDOM%.txt"
set /a PASSED=0
set /a FAILED=0

where "%VIPER_BIN%" >nul 2>nul
if errorlevel 1 if not exist "%VIPER_BIN%" (
    echo Ridgebound probes: Viper binary not found: %VIPER_BIN% 1>&2
    echo Set VIPER_BIN to an existing executable; this runner never builds it. 1>&2
    exit /b 1
)

pushd "%REPO_ROOT%" || exit /b 1
"%VIPER_BIN%" check "%DEMO_DIR%" --diagnostic-format=json >"%OUTPUT%" 2>&1
if errorlevel 1 (
    type "%OUTPUT%"
    echo Ridgebound probes: project check failed 1>&2
    popd
    del /q "%OUTPUT%" >nul 2>nul
    exit /b 1
)

for %%P in (topology_probe traversal_probe state_probe smoke_probe) do call :run_probe %%P
popd
del /q "%OUTPUT%" >nul 2>nul
echo Ridgebound probes: !PASSED! passed, !FAILED! failed
if not "!FAILED!"=="0" exit /b 1
exit /b 0

:run_probe
echo ==^> %1
"%VIPER_BIN%" run "%DEMO_DIR%%1.zia" >"%OUTPUT%" 2>&1
set "PROBE_STATUS=%ERRORLEVEL%"
type "%OUTPUT%"
if not "%PROBE_STATUS%"=="0" goto :probe_failed
findstr /c:"RESULT: ok" "%OUTPUT%" >nul
if errorlevel 1 goto :probe_failed
set /a PASSED+=1
goto :eof

:probe_failed
echo PROBE FAILED: %1 ^(exit %PROBE_STATUS%^) 1>&2
set /a FAILED+=1
goto :eof
