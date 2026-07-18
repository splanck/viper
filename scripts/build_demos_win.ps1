#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/build_demos_win.ps1
# Purpose: Build and stage every curated Zia showcase demo on Windows.
# Key invariants:
#   - The shared demo manifest is the single project inventory.
#   - Host and target tool trees remain distinct for cross-architecture builds.
#   - An asset-stage or native-link failure contributes to the final exit code.
# Ownership/Lifetime: Native binaries and declared assets are owned by examples/bin.
# Links: scripts/demo_projects.list, scripts/build_demos.sh
# Cross-platform touchpoints: Architecture aliases match the Unix demo driver;
#                             CMake's Windows generator selects x64 or ARM64.
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
    Write-Host "Usage: build_demos_win.ps1 [--clean] [--arch arm64|x64]"
    Write-Host "  --clean    Remove existing binaries before building"
    Write-Host "  --arch     Target architecture (default: host, or ZANNA_DEMO_ARCH)"
}

$clean = $false
$requestedArch = [Environment]::GetEnvironmentVariable("ZANNA_DEMO_ARCH", "Process")
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
$buildDirSetting = [Environment]::GetEnvironmentVariable("ZANNA_DEMO_BUILD_DIR", "Process")
$buildDirExplicit = -not [string]::IsNullOrWhiteSpace($buildDirSetting)
if (-not $buildDirExplicit) {
    $buildDirSetting = [Environment]::GetEnvironmentVariable("ZANNA_BUILD_DIR", "Process")
    $buildDirExplicit = -not [string]::IsNullOrWhiteSpace($buildDirSetting)
}
if (-not $buildDirExplicit) {
    $buildDirSetting = Join-Path $repoRoot "build"
}

$toolBuildDirSetting = [Environment]::GetEnvironmentVariable("ZANNA_DEMO_TOOL_BUILD_DIR", "Process")
$toolBuildDirExplicit = -not [string]::IsNullOrWhiteSpace($toolBuildDirSetting)
if (-not $toolBuildDirExplicit) {
    $toolBuildDirSetting = Join-Path $repoRoot "build"
}

$buildType = Get-EnvironmentValue -Name "ZANNA_BUILD_TYPE" -Default "Debug"
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
    "aarch64" { $demoArch = "arm64" }
    "arm64" { $demoArch = "arm64" }
    "amd64" { $demoArch = "x64" }
    "x86_64" { $demoArch = "x64" }
    "x64" { $demoArch = "x64" }
    default {
        Write-Error "Invalid demo architecture '$requestedArch'; expected arm64 or x64."
        exit 1
    }
}

if (-not $buildDirExplicit -and $demoArch -ne $hostArch) {
    $buildDirSetting = Join-Path $repoRoot "build-$demoArch"
}
if (-not $toolBuildDirExplicit -and $demoArch -eq $hostArch) {
    $toolBuildDirSetting = $buildDirSetting
}

$buildDir = Get-FullPath -Path $buildDirSetting -Base $repoRoot
$toolBuildDir = Get-FullPath -Path $toolBuildDirSetting -Base $repoRoot
$binDir = Join-Path $repoRoot "examples\bin"
$manifest = Join-Path $scriptRoot "demo_projects.list"
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

function Copy-DemoAsset {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectDir,
        [Parameter(Mandatory = $true)][string]$SourceRelative,
        [Parameter(Mandatory = $true)][string]$TargetRelative
    )

    $source = Join-Path $ProjectDir $SourceRelative
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Asset not found: $SourceRelative"
    }
    $destination = if ($TargetRelative -eq ".") {
        $binDir
    } else {
        Join-Path $binDir $TargetRelative
    }
    [void](New-Item -ItemType Directory -Path $destination -Force)
    if (Test-Path -LiteralPath $source -PathType Container) {
        foreach ($child in Get-ChildItem -LiteralPath $source -Force) {
            Copy-Item -LiteralPath $child.FullName -Destination $destination -Recurse -Force
        }
    } else {
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
}

function Stage-DemoAssets {
    param([Parameter(Mandatory = $true)][string]$ProjectDir)

    $projectFile = Join-Path $ProjectDir "zanna.project"
    foreach ($line in Get-Content -LiteralPath $projectFile) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) {
            continue
        }
        $parts = $trimmed -split '\s+', 3
        if ($parts.Count -lt 2 -or $parts[0] -ine "asset") {
            continue
        }
        $sourceRelative = $parts[1].Trim('"')
        $targetRelative = if ($parts.Count -ge 3) { $parts[2].Trim().Trim('"') } else { "." }
        Copy-DemoAsset -ProjectDir $ProjectDir -SourceRelative $sourceRelative `
            -TargetRelative $targetRelative
    }
}

function Build-Demo {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$ProjectDir
    )

    Write-Host "Building $Name..."
    Write-Host "  Started: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')"
    if ($Name -ieq "zannasql") {
        Write-Host "  Note: zannasql is the slowest demo on Windows and can take several minutes."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectDir "zanna.project") -PathType Leaf)) {
        Write-Host "  ERROR: No zanna.project found in $ProjectDir"
        Write-Host "  Finished: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')"
        Write-Host ""
        return $false
    }

    $output = Join-Path $binDir "$Name.exe"
    $buildArguments = @("build", $ProjectDir, "--arch", $demoArch)
    if ($Name -ieq "zannasql") {
        Write-Host "  Using -O0 to avoid pathological optimizer/codegen time for this large demo."
        $buildArguments += "-O0"
    } elseif ($Name -ieq "xenoscape") {
        Write-Host "  Using -O0 to avoid the Windows x64 checked-integer optimizer miscompile."
        $buildArguments += "-O0"
    }
    $buildArguments += @("-o", $output)

    Write-Host "  Compiling..."
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        # Windows PowerShell 5.1 wraps redirected native stderr as non-terminating
        # NativeCommandError records. Zanna reports normal linker progress there,
        # so rely on its process status while retaining strict mode for script code.
        $ErrorActionPreference = "Continue"
        & $zanna @buildArguments 2>$null | ForEach-Object { Write-Host $_ }
        $buildStatus = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($buildStatus -ne 0) {
        Write-Host "  FAILED"
        try {
            $ErrorActionPreference = "Continue"
            & $zanna @buildArguments 2>&1 | ForEach-Object { Write-Host $_ }
        } finally {
            $ErrorActionPreference = $savedErrorActionPreference
        }
        Write-Host "  Finished: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')"
        Write-Host ""
        return $false
    }
    try {
        Stage-DemoAssets -ProjectDir $ProjectDir
    } catch {
        Write-Host "  ERROR: $($_.Exception.Message)"
        Write-Host "  FAILED"
        Write-Host "  Finished: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')"
        Write-Host ""
        return $false
    }

    Write-Host "  OK"
    Write-Host "  Built: $output"
    Write-Host "  Finished: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')"
    Write-Host ""
    return $true
}

$previousBuildDir = [Environment]::GetEnvironmentVariable("ZANNA_BUILD_DIR", "Process")
$env:ZANNA_BUILD_DIR = $buildDir
Push-Location $repoRoot
try {
    Write-Host "Building Zanna demos as native $demoArch binaries"
    Write-Host "=============================================="
    Write-Host ""
    Write-Host "Note: larger demos can stay quiet for several minutes while codegen runs."
    Write-Host "Using Zanna tool: $toolBuildDir"
    Write-Host "Using target runtime build: $buildDir"
    Write-Host ""

    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Demo manifest not found: $manifest"
    }
    if (-not (Test-Path -LiteralPath $zanna -PathType Leaf)) {
        Ensure-ZannaBuild -Tree $toolBuildDir -Architecture $hostArch `
            -TreeIsExplicit $toolBuildDirExplicit -ExpectedExecutable $zanna `
            -Description "host Zanna tool"
    }
    if ($toolBuildDir -ine $buildDir -and
        -not (Test-Path -LiteralPath $targetZanna -PathType Leaf)) {
        Ensure-ZannaBuild -Tree $buildDir -Architecture $demoArch `
            -TreeIsExplicit $buildDirExplicit -ExpectedExecutable $targetZanna `
            -Description "target-architecture Zanna runtime"
    }

    [void](New-Item -ItemType Directory -Path $binDir -Force)
    if ($clean) {
        Write-Host "Cleaning existing binaries..."
        $expectedBin = [IO.Path]::GetFullPath((Join-Path $repoRoot "examples\bin"))
        if ([IO.Path]::GetFullPath($binDir) -ne $expectedBin) {
            throw "Refusing to clean unexpected demo directory: $binDir"
        }
        foreach ($entry in Get-ChildItem -LiteralPath $binDir -Force) {
            Remove-Item -LiteralPath $entry.FullName -Recurse -Force
        }
    }

    Write-Host "=== Zia Showcase Demos ==="
    Write-Host ""
    $failed = 0
    $succeeded = 0
    $entries = 0
    foreach ($line in Get-Content -LiteralPath $manifest) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) {
            continue
        }
        ++$entries
        $fields = $trimmed.Split('|')
        if ($fields.Count -ne 3) {
            Write-Host "ERROR: invalid demo manifest entry: $trimmed"
            ++$failed
            continue
        }
        $name = $fields[0].Trim()
        $category = $fields[1].Trim()
        $directory = $fields[2].Trim()
        if ($category -ieq "games") {
            $projectDir = Join-Path $repoRoot "examples\games\$directory"
        } elseif ($category -ieq "apps") {
            $projectDir = Join-Path $repoRoot "examples\apps\$directory"
        } else {
            Write-Host "ERROR: invalid demo category '$category' for '$name'"
            ++$failed
            continue
        }
        if (Build-Demo -Name $name -ProjectDir $projectDir) {
            ++$succeeded
        } else {
            ++$failed
        }
    }
    if ($entries -eq 0) {
        throw "Demo manifest contains no projects."
    }

    Write-Host "=============================================="
    if ($failed -eq 0) {
        Write-Host "All $succeeded demos built successfully."
        Write-Host ""
        Write-Host "Binaries are in: $binDir"
        Get-ChildItem -LiteralPath $binDir | Format-Table Mode, LastWriteTime, Length, Name
        exit 0
    }
    Write-Host "$failed demo(s) failed, $succeeded succeeded"
    exit $failed
} finally {
    Pop-Location
    [Environment]::SetEnvironmentVariable("ZANNA_BUILD_DIR", $previousBuildDir, "Process")
}
