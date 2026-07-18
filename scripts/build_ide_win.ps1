#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/build_ide_win.ps1
# Purpose: Build Zanna Studio as a standalone native Windows binary.
# Key invariants:
#   - Host and target Zanna trees remain distinct for cross-architecture builds.
#   - Build metadata describes the exact output and source state.
#   - The compatibility copy is skipped only when explicitly disabled.
# Ownership/Lifetime: The requested binary, metadata, and compatibility copy
#                     are owned by this invocation.
# Links: src/zannastudio/zanna.project, scripts/build_ide.sh
# Cross-platform touchpoints: Architecture aliases and metadata match the Unix
#                             driver; Windows CMake generators select x64/ARM64.
#
#===----------------------------------------------------------------------===#

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

function Get-EnvironmentValue {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Default
    )
    $value = [Environment]::GetEnvironmentVariable($Name, "Process")
    if ([string]::IsNullOrEmpty($value)) {
        return $Default
    }
    return $value
}

function ConvertFrom-NativeArgumentString {
    param([AllowEmptyString()][string]$Value)

    $result = [Collections.Generic.List[string]]::new()
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $result.ToArray()
    }
    $current = [Text.StringBuilder]::new()
    [char]$quote = [char]0
    for ($index = 0; $index -lt $Value.Length; ++$index) {
        $character = $Value[$index]
        if ($quote -ne [char]0) {
            if ($character -eq $quote) {
                $quote = [char]0
            } elseif ($character -eq '\' -and $index + 1 -lt $Value.Length -and
                      $Value[$index + 1] -eq $quote) {
                [void]$current.Append($quote)
                ++$index
            } else {
                [void]$current.Append($character)
            }
        } elseif ($character -eq '"' -or $character -eq "'") {
            $quote = $character
        } elseif ([char]::IsWhiteSpace($character)) {
            if ($current.Length -gt 0) {
                $result.Add($current.ToString())
                [void]$current.Clear()
            }
        } else {
            [void]$current.Append($character)
        }
    }
    if ($quote -ne [char]0) {
        throw "ZANNA_EXTRA_CMAKE_ARGS contains an unterminated quoted argument."
    }
    if ($current.Length -gt 0) {
        $result.Add($current.ToString())
    }
    return $result.ToArray()
}

function Get-FullPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Base
    )
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Invoke-CheckedNative {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][string]$FailureMessage
    )
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FailureMessage (exit $LASTEXITCODE)"
    }
}

function Show-Usage {
    Write-Host "Usage: build_ide_win.ps1 [--clean] [--arch arm64|x64] [--output PATH]"
    Write-Host "  --clean        Remove the existing Zanna Studio binary before building"
    Write-Host "  --arch         Target architecture (default: host, or ZANNA_IDE_ARCH)"
    Write-Host "  --output PATH  Write the binary to PATH (default: src\zannastudio\bin\zannastudio.exe)"
    Write-Host "  Compatibility copy: build\zannastudio\zannastudio.exe unless ZANNA_IDE_SKIP_COMPAT_COPY=1"
}

$invocationRoot = (Get-Location).Path
$clean = $false
$requestedArch = [Environment]::GetEnvironmentVariable("ZANNA_IDE_ARCH", "Process")
if ([string]::IsNullOrWhiteSpace($requestedArch)) {
    $requestedArch = [Environment]::GetEnvironmentVariable("ZANNA_DEMO_ARCH", "Process")
}
$outputOverride = [Environment]::GetEnvironmentVariable("ZANNA_IDE_OUTPUT", "Process")
$argumentIndex = 0
while ($argumentIndex -lt $args.Count) {
    $argument = [string]$args[$argumentIndex]
    switch -CaseSensitive ($argument) {
        "--clean" {
            $clean = $true
            ++$argumentIndex
        }
        "--arch" {
            if ($argumentIndex + 1 -ge $args.Count) {
                Write-Error "--arch requires arm64 or x64"
                exit 1
            }
            $requestedArch = [string]$args[$argumentIndex + 1]
            $argumentIndex += 2
        }
        "--output" {
            if ($argumentIndex + 1 -ge $args.Count) {
                Write-Error "--output requires a path"
                exit 1
            }
            $outputOverride = [string]$args[$argumentIndex + 1]
            $argumentIndex += 2
        }
        { $_ -eq "-h" -or $_ -eq "--help" } {
            Show-Usage
            exit 0
        }
        default {
            Write-Error "Unknown argument: $argument"
            Show-Usage
            exit 1
        }
    }
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot ".."))
$ideDir = Join-Path $repoRoot "src\zannastudio"

$buildDirSetting = [Environment]::GetEnvironmentVariable("ZANNA_IDE_BUILD_DIR", "Process")
$buildDirExplicit = -not [string]::IsNullOrWhiteSpace($buildDirSetting)
if (-not $buildDirExplicit) {
    $buildDirSetting = [Environment]::GetEnvironmentVariable("ZANNA_BUILD_DIR", "Process")
    $buildDirExplicit = -not [string]::IsNullOrWhiteSpace($buildDirSetting)
}
if (-not $buildDirExplicit) {
    $buildDirSetting = Join-Path $repoRoot "build"
}

$toolBuildDirSetting = [Environment]::GetEnvironmentVariable("ZANNA_IDE_TOOL_BUILD_DIR", "Process")
$toolBuildDirExplicit = -not [string]::IsNullOrWhiteSpace($toolBuildDirSetting)
if (-not $toolBuildDirExplicit) {
    $toolBuildDirSetting = Join-Path $repoRoot "build"
}

$buildType = Get-EnvironmentValue -Name "ZANNA_BUILD_TYPE" -Default "Release"
$jobsValue = Get-EnvironmentValue -Name "JOBS" `
    -Default (Get-EnvironmentValue -Name "NUMBER_OF_PROCESSORS" -Default "8")
$jobs = 0
if (-not [int]::TryParse($jobsValue, [ref]$jobs) -or $jobs -lt 1) {
    throw "JOBS must be a positive integer; received '$jobsValue'."
}

$hostArch = if ([Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITECTURE", "Process") -ieq
                   "ARM64") {
    "arm64"
} else {
    "x64"
}
if ([string]::IsNullOrWhiteSpace($requestedArch)) {
    $requestedArch = $hostArch
}
switch ($requestedArch.ToLowerInvariant()) {
    "aarch64" { $ideArch = "arm64" }
    "arm64" { $ideArch = "arm64" }
    "amd64" { $ideArch = "x64" }
    "x86_64" { $ideArch = "x64" }
    "x64" { $ideArch = "x64" }
    default {
        Write-Error "Invalid IDE architecture '$requestedArch'; expected arm64 or x64."
        exit 1
    }
}

if (-not $buildDirExplicit -and $ideArch -ne $hostArch) {
    $buildDirSetting = Join-Path $repoRoot "build-$ideArch"
}
if (-not $toolBuildDirExplicit -and $ideArch -eq $hostArch) {
    $toolBuildDirSetting = $buildDirSetting
}

$buildDir = Get-FullPath -Path $buildDirSetting -Base $repoRoot
$toolBuildDir = Get-FullPath -Path $toolBuildDirSetting -Base $repoRoot
$ideBinDirSetting = Get-EnvironmentValue -Name "ZANNA_IDE_OUT_DIR" `
    -Default (Join-Path $ideDir "bin")
$ideBinDir = Get-FullPath -Path $ideBinDirSetting -Base $invocationRoot
if ([string]::IsNullOrWhiteSpace($outputOverride)) {
    $outputFile = Join-Path $ideBinDir "zannastudio.exe"
} else {
    $outputFile = Get-FullPath -Path $outputOverride -Base $invocationRoot
}

$compatSetting = [Environment]::GetEnvironmentVariable("ZANNA_IDE_COMPAT_OUTPUT", "Process")
if ([string]::IsNullOrWhiteSpace($compatSetting)) {
    $compatOutput = Join-Path $buildDir "zannastudio\zannastudio.exe"
} else {
    $compatOutput = Get-FullPath -Path $compatSetting -Base $invocationRoot
}
$skipCompatCopy = Get-EnvironmentValue -Name "ZANNA_IDE_SKIP_COMPAT_COPY" -Default "0"
$zanna = Join-Path $toolBuildDir "src\tools\zanna\$buildType\zanna.exe"
$targetZanna = Join-Path $buildDir "src\tools\zanna\$buildType\zanna.exe"

function Ensure-ZannaBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Tree,
        [Parameter(Mandatory = $true)][string]$Architecture,
        [Parameter(Mandatory = $true)][bool]$TreeIsExplicit,
        [Parameter(Mandatory = $true)][string]$ExpectedExecutable,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Write-Host "$Description not found at $ExpectedExecutable"
    Write-Host "Configuring/building $Description..."
    $configureArguments = @("-S", $repoRoot, "-B", $Tree)
    $generator = [Environment]::GetEnvironmentVariable("ZANNA_CMAKE_GENERATOR", "Process")
    if (-not [string]::IsNullOrWhiteSpace($generator)) {
        $configureArguments += @("-G", $generator)
    } elseif (-not $TreeIsExplicit) {
        $cmakeArch = if ($Architecture -eq "arm64") { "ARM64" } else { "x64" }
        $configureArguments += @("-A", $cmakeArch)
    }
    $configureArguments += "-DCMAKE_BUILD_TYPE=$buildType"
    $configureArguments += @(ConvertFrom-NativeArgumentString -Value `
        ([Environment]::GetEnvironmentVariable("ZANNA_EXTRA_CMAKE_ARGS", "Process")))
    Invoke-CheckedNative -FilePath "cmake" -Arguments $configureArguments `
        -FailureMessage "CMake configuration failed for $Description"
    Invoke-CheckedNative -FilePath "cmake" `
        -Arguments @("--build", $Tree, "--config", $buildType, "--target", "zanna", "-j", [string]$jobs) `
        -FailureMessage "$Description build failed"
    if (-not (Test-Path -LiteralPath $ExpectedExecutable -PathType Leaf)) {
        throw "$Description still not found at $ExpectedExecutable"
    }
}

function Write-BuildInfo {
    param([Parameter(Mandatory = $true)][string]$Binary)

    $infoPath = Join-Path (Split-Path -Parent $Binary) "zannastudio.buildinfo"
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $revision = (& git -C $repoRoot rev-parse --short HEAD 2>$null | Select-Object -First 1)
        & git -C $repoRoot diff --quiet --ignore-submodules -- 2>$null
        $diffStatus = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ([string]::IsNullOrWhiteSpace($revision)) {
        $revision = "unknown"
    }
    $dirty = if ($diffStatus -eq 0) { "" } else { " dirty" }
    $lines = @(
        "Build: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')",
        "Source: $revision$dirty",
        "Output: $Binary",
        "Zanna: $zanna"
    )
    [IO.File]::WriteAllLines($infoPath, $lines, [Text.UTF8Encoding]::new($false))
    return $infoPath
}

$previousBuildDir = [Environment]::GetEnvironmentVariable("ZANNA_BUILD_DIR", "Process")
$env:ZANNA_BUILD_DIR = $buildDir
Push-Location $repoRoot
try {
    Write-Host "Building Zanna Studio as a native $ideArch binary"
    Write-Host "=============================================="
    Write-Host ""
    Write-Host "Using Zanna tool: $toolBuildDir"
    Write-Host "Using target runtime build: $buildDir"
    Write-Host "Source: $ideDir"
    Write-Host "Output: $outputFile"
    Write-Host ""

    if (-not (Test-Path -LiteralPath (Join-Path $ideDir "zanna.project") -PathType Leaf)) {
        throw "No zanna.project found in $ideDir"
    }
    if (-not (Test-Path -LiteralPath $zanna -PathType Leaf)) {
        Ensure-ZannaBuild -Tree $toolBuildDir -Architecture $hostArch `
            -TreeIsExplicit $toolBuildDirExplicit -ExpectedExecutable $zanna `
            -Description "host Zanna tool"
    }
    if ($toolBuildDir -ine $buildDir -and
        -not (Test-Path -LiteralPath $targetZanna -PathType Leaf)) {
        Ensure-ZannaBuild -Tree $buildDir -Architecture $ideArch `
            -TreeIsExplicit $buildDirExplicit -ExpectedExecutable $targetZanna `
            -Description "target-architecture Zanna runtime"
    }

    $outputDir = Split-Path -Parent $outputFile
    [void](New-Item -ItemType Directory -Path $outputDir -Force)
    if ($clean) {
        Write-Host "Cleaning existing Zanna Studio binary..."
        foreach ($path in @(
                $outputFile,
                (Join-Path $outputDir "zannastudio.buildinfo"),
                $compatOutput,
                (Join-Path (Split-Path -Parent $compatOutput) "zannastudio.buildinfo"))) {
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                Remove-Item -LiteralPath $path -Force
            }
        }
    }

    $buildArguments = @("build", $ideDir, "--arch", $ideArch, "-o", $outputFile)
    Write-Host "Compiling..."
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        # Windows PowerShell 5.1 promotes redirected native stderr to error
        # records even when Zanna is only reporting normal linker progress.
        $ErrorActionPreference = "Continue"
        & $zanna @buildArguments 2>$null | ForEach-Object { Write-Host $_ }
        $buildStatus = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($buildStatus -ne 0) {
        Write-Host "FAILED"
        try {
            $ErrorActionPreference = "Continue"
            & $zanna @buildArguments 2>&1 | ForEach-Object { Write-Host $_ }
        } finally {
            $ErrorActionPreference = $savedErrorActionPreference
        }
        exit 1
    }

    Write-Host "OK"
    $buildInfo = Write-BuildInfo -Binary $outputFile
    if ($skipCompatCopy -ne "1" -and $outputFile -ine $compatOutput) {
        $compatDir = Split-Path -Parent $compatOutput
        [void](New-Item -ItemType Directory -Path $compatDir -Force)
        Copy-Item -LiteralPath $outputFile -Destination $compatOutput -Force
        [void](Write-BuildInfo -Binary $compatOutput)
        Write-Host "Compatibility copy: $compatOutput"
    }
    Write-Host "Built: $outputFile"
    Write-Host "Build info: $buildInfo"
    exit 0
} finally {
    Pop-Location
    [Environment]::SetEnvironmentVariable("ZANNA_BUILD_DIR", $previousBuildDir, "Process")
}
