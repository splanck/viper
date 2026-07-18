#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/build_installer.ps1
# Purpose: Build or reuse a staged Zanna tree and invoke install-package.
# Key invariants:
#   - Fresh package builds use the canonical Windows build script.
#   - Installer stages include ZannaIDE unless the caller chose explicitly.
#   - All install-package arguments are forwarded as discrete native arguments.
# Ownership/Lifetime: Build and package outputs are owned by their caller-selected paths.
# Links: scripts/build_zanna_win.ps1, docs/installer-release.md
# Cross-platform touchpoints: This Windows wrapper selects the native installer
#                             target; packaging policy lives in install-package.
#
#===----------------------------------------------------------------------===#

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot ".."))
$forwardArguments = @($args | ForEach-Object { [string]$_ })

$buildDir = [Environment]::GetEnvironmentVariable("ZANNA_BUILD_DIR", "Process")
if ([string]::IsNullOrWhiteSpace($buildDir)) {
    $buildDir = Join-Path $repoRoot "build"
    $env:ZANNA_BUILD_DIR = $buildDir
} elseif (-not [IO.Path]::IsPathRooted($buildDir)) {
    $buildDir = [IO.Path]::GetFullPath((Join-Path $repoRoot $buildDir))
    $env:ZANNA_BUILD_DIR = $buildDir
}

$buildType = [Environment]::GetEnvironmentVariable("ZANNA_BUILD_TYPE", "Process")
if ([string]::IsNullOrWhiteSpace($buildType)) {
    $buildType = "Release"
    $env:ZANNA_BUILD_TYPE = $buildType
}
if ([string]::IsNullOrWhiteSpace(
        [Environment]::GetEnvironmentVariable("ZANNA_SKIP_INSTALL", "Process"))) {
    $env:ZANNA_SKIP_INSTALL = "1"
}

$usesExistingInput = $false
$hasExplicitBuildDir = $false
foreach ($argument in $forwardArguments) {
    if ($argument -ieq "--build-dir") {
        $hasExplicitBuildDir = $true
    } elseif ($argument -ieq "--stage-dir" -or $argument -ieq "--verify-only") {
        $usesExistingInput = $true
    }
}

if (-not $usesExistingInput) {
    $extraArguments = [Environment]::GetEnvironmentVariable("ZANNA_EXTRA_CMAKE_ARGS", "Process")
    if ($extraArguments -notmatch '(?i)(?:^|\s)-DZANNA_INSTALL_ZANNAIDE=') {
        if ([string]::IsNullOrWhiteSpace($extraArguments)) {
            $env:ZANNA_EXTRA_CMAKE_ARGS = "-DZANNA_INSTALL_ZANNAIDE=ON"
        } else {
            $env:ZANNA_EXTRA_CMAKE_ARGS = "$extraArguments -DZANNA_INSTALL_ZANNAIDE=ON"
        }
    }

    $buildScript = Join-Path $scriptRoot "build_zanna_win.ps1"
    if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
        throw "Canonical Windows build script not found: $buildScript"
    }
    $powerShellHost = Join-Path $PSHOME "pwsh.exe"
    if (-not (Test-Path -LiteralPath $powerShellHost -PathType Leaf)) {
        $powerShellHost = Join-Path $PSHOME "powershell.exe"
    }
    if (-not (Test-Path -LiteralPath $powerShellHost -PathType Leaf)) {
        throw "Unable to locate the current PowerShell host under $PSHOME."
    }
    & $powerShellHost -NoProfile -ExecutionPolicy Bypass -File $buildScript
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$zanna = Join-Path $buildDir "src\tools\zanna\zanna.exe"
$configuredZanna = Join-Path $buildDir "src\tools\zanna\$buildType\zanna.exe"
if (Test-Path -LiteralPath $configuredZanna -PathType Leaf) {
    $zanna = $configuredZanna
}
if (-not (Test-Path -LiteralPath $zanna -PathType Leaf)) {
    Write-Error "Zanna executable not found at '$zanna'. Build Zanna first or set ZANNA_BUILD_DIR."
    exit 1
}

$packageArguments = @("install-package")
if (-not $usesExistingInput -and -not $hasExplicitBuildDir) {
    $packageArguments += @(
        "--build-dir", $buildDir,
        "--config", $buildType,
        "--skip-build"
    )
}
$packageArguments += $forwardArguments
& $zanna @packageArguments
exit $LASTEXITCODE
