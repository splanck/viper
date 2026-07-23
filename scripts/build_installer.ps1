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
#   - Installer stages include Zanna Studio unless the caller chose explicitly.
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
if (@($forwardArguments | Where-Object { $_ -ieq "--help" -or $_ -eq "-h" }).Count -gt 0) {
    Write-Host "Usage: build_installer.ps1 [zanna install-package options]"
    Write-Host "Builds a Release toolchain package with Zanna Studio unless an existing input is supplied."
    Write-Host "Use --stage-dir, --build-dir, or --verify-only to reuse caller-owned input."
    exit 0
}

$buildDir = [Environment]::GetEnvironmentVariable("ZANNA_BUILD_DIR", "Process")
if ([string]::IsNullOrWhiteSpace($buildDir)) {
    $buildDir = Join-Path $repoRoot "build"
} elseif (-not [IO.Path]::IsPathRooted($buildDir)) {
    $buildDir = [IO.Path]::GetFullPath((Join-Path $repoRoot $buildDir))
} else {
    $buildDir = [IO.Path]::GetFullPath($buildDir)
}
$env:ZANNA_BUILD_DIR = $buildDir

$buildType = [Environment]::GetEnvironmentVariable("ZANNA_BUILD_TYPE", "Process")
if ([string]::IsNullOrWhiteSpace($buildType)) {
    $buildType = "Release"
}
switch ($buildType.ToLowerInvariant()) {
    "release" { $buildType = "Release" }
    "relwithdebinfo" { $buildType = "RelWithDebInfo" }
    default { throw "Windows installer builds require ZANNA_BUILD_TYPE=Release or RelWithDebInfo." }
}
$env:ZANNA_BUILD_TYPE = $buildType
if ([string]::IsNullOrWhiteSpace(
        [Environment]::GetEnvironmentVariable("ZANNA_SKIP_INSTALL", "Process"))) {
    $env:ZANNA_SKIP_INSTALL = "1"
}

$usesExistingInput = $false
$hasExplicitBuildDir = $false
foreach ($argument in $forwardArguments) {
    if ($argument -ieq "--build-dir" -or
        $argument.StartsWith("--build-dir=", [StringComparison]::OrdinalIgnoreCase)) {
        $hasExplicitBuildDir = $true
        $usesExistingInput = $true
    } elseif ($argument -ieq "--stage-dir" -or $argument -ieq "--verify-only" -or
              $argument.StartsWith("--stage-dir=", [StringComparison]::OrdinalIgnoreCase) -or
              $argument.StartsWith("--verify-only=", [StringComparison]::OrdinalIgnoreCase)) {
        $usesExistingInput = $true
    }
}

if (-not $usesExistingInput) {
    $extraArguments = [Environment]::GetEnvironmentVariable("ZANNA_EXTRA_CMAKE_ARGS", "Process")
    $requireZannaStudio = $false
    $studioSettings = [regex]::Matches(
        [string]$extraArguments,
        '(?i)(?:^|\s)["'']?-DZANNA_INSTALL_ZANNASTUDIO=([^"''\s]+)["'']?(?=\s|$)'
    )
    if ($studioSettings.Count -eq 0) {
        if ([string]::IsNullOrWhiteSpace($extraArguments)) {
            $env:ZANNA_EXTRA_CMAKE_ARGS = "-DZANNA_INSTALL_ZANNASTUDIO=ON"
        } else {
            $env:ZANNA_EXTRA_CMAKE_ARGS = "$extraArguments -DZANNA_INSTALL_ZANNASTUDIO=ON"
        }
        $requireZannaStudio = $true
    } else {
        $studioSetting = $studioSettings[$studioSettings.Count - 1].Groups[1].Value
        if ($studioSetting -match '^(?i:ON|1|TRUE|YES)$') {
            $requireZannaStudio = $true
        } elseif ($studioSetting -notmatch '^(?i:OFF|0|FALSE|NO)$') {
            throw "Unsupported ZANNA_INSTALL_ZANNASTUDIO value '$studioSetting'."
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
    if ($requireZannaStudio) {
        $studio = Join-Path $buildDir "zannastudio\zannastudio.exe"
        $studioBuildInfo = Join-Path $buildDir "zannastudio\zannastudio.buildinfo"
        if (-not (Test-Path -LiteralPath $studio -PathType Leaf) -or
            -not (Test-Path -LiteralPath $studioBuildInfo -PathType Leaf)) {
            throw "The Windows toolchain build did not produce the required Zanna Studio executable and build metadata."
        }
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
