#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/build_demos_win.ps1
# Purpose: Build, stage, and optionally smoke-run every curated Zia showcase demo on Windows.
# Key invariants:
#   - The shared demo manifest is the single project inventory.
#   - Host and target tool trees remain distinct for cross-architecture builds.
#   - Existing CMake trees are built without mutating their generator platform.
#   - Build, asset-stage, and requested launch failures contribute to the final exit code.
# Ownership/Lifetime: Native binaries and declared assets are owned by examples/bin;
#                     launch logs are temporary and run-created artifacts are removed.
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

function Test-PathWithin {
    param(
        [Parameter(Mandatory = $true)][string]$Base,
        [Parameter(Mandatory = $true)][string]$Candidate,
        [switch]$AllowBase
    )

    $baseFull = [IO.Path]::GetFullPath($Base).TrimEnd('\', '/')
    $candidateFull = [IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
    if ($AllowBase -and
        [string]::Equals($baseFull, $candidateFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    $prefix = $baseFull + [IO.Path]::DirectorySeparatorChar
    return $candidateFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
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
    Write-Host "Usage: build_demos_win.ps1 [--clean] [--run|--skip-run] [--arch arm64|x64]"
    Write-Host "  --clean      Remove existing binaries before building"
    Write-Host "  --run        Launch each built demo for smoke validation"
    Write-Host "  --skip-run   Build only; skip launch validation (default)"
    Write-Host "  --arch       Target architecture (default: host, or ZANNA_DEMO_ARCH)"
}

$clean = $false
$run = $false
$requestedArch = [Environment]::GetEnvironmentVariable("ZANNA_DEMO_ARCH", "Process")
$argumentIndex = 0
while ($argumentIndex -lt $args.Count) {
    $argument = [string]$args[$argumentIndex]
    switch -CaseSensitive ($argument) {
        "--clean" {
            $clean = $true
            ++$argumentIndex
        }
        "--run" {
            $run = $true
            ++$argumentIndex
        }
        "--skip-run" {
            $run = $false
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
if ($buildType -notin @("Debug", "Release", "RelWithDebInfo", "MinSizeRel")) {
    throw "ZANNA_BUILD_TYPE must be Debug, Release, RelWithDebInfo, or MinSizeRel; received '$buildType'."
}
$jobsValue = Get-EnvironmentValue -Name "JOBS" `
    -Default (Get-EnvironmentValue -Name "NUMBER_OF_PROCESSORS" -Default "8")
$jobs = 0
if (-not [int]::TryParse($jobsValue, [ref]$jobs) -or $jobs -lt 1) {
    throw "JOBS must be a positive integer; received '$jobsValue'."
}

$nativeArchitecture = [Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITEW6432", "Process")
if ([string]::IsNullOrWhiteSpace($nativeArchitecture)) {
    $nativeArchitecture = [Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITECTURE", "Process")
}
$hostArch = if ($nativeArchitecture -ieq "ARM64") {
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

if ($run -and $demoArch -ne $hostArch) {
    throw "Cannot smoke-run $demoArch demos on the $hostArch host. Omit --run for cross-architecture builds."
}

$runTimeoutSeconds = 5
if ($run) {
    $runTimeoutValue = Get-EnvironmentValue -Name "ZANNA_DEMO_TIMEOUT" -Default "5"
    if (-not [int]::TryParse($runTimeoutValue, [ref]$runTimeoutSeconds) -or
        $runTimeoutSeconds -lt 1 -or $runTimeoutSeconds -gt 2147483) {
        throw "ZANNA_DEMO_TIMEOUT must be an integer from 1 through 2147483; received '$runTimeoutValue'."
    }
}

$windowsO0Demos = @(
    "3dbowling",
    "ridgebound",
    "xenoscape",
    "crackman",
    "chess",
    "zannasql"
)

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

function Resolve-ZannaExecutable {
    param([Parameter(Mandatory = $true)][string]$Tree)

    $configured = Join-Path $Tree "src\tools\zanna\$buildType\zanna.exe"
    $singleConfig = Join-Path $Tree "src\tools\zanna\zanna.exe"
    if (Test-Path -LiteralPath $configured -PathType Leaf) {
        return $configured
    }
    if (Test-Path -LiteralPath $singleConfig -PathType Leaf) {
        return $singleConfig
    }
    return $configured
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)][string]$Cache,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $prefix = "${Name}:"
    foreach ($line in Get-Content -LiteralPath $Cache) {
        if ($line.StartsWith($prefix, [StringComparison]::Ordinal)) {
            $separator = $line.IndexOf('=')
            if ($separator -ge 0) {
                return $line.Substring($separator + 1).Trim()
            }
        }
    }
    return ""
}

function Assert-CMakeTreeArchitecture {
    param(
        [Parameter(Mandatory = $true)][string]$Cache,
        [Parameter(Mandatory = $true)][string]$Architecture,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $reported = Get-CMakeCacheValue -Cache $Cache -Name "CMAKE_GENERATOR_PLATFORM"
    if ([string]::IsNullOrWhiteSpace($reported)) {
        $reported = Get-CMakeCacheValue -Cache $Cache -Name "CMAKE_SYSTEM_PROCESSOR"
    }
    if ([string]::IsNullOrWhiteSpace($reported)) {
        return
    }
    $normalized = switch ($reported.ToLowerInvariant()) {
        "arm64" { "arm64" }
        "aarch64" { "arm64" }
        "x64" { "x64" }
        "amd64" { "x64" }
        "x86_64" { "x64" }
        default { "" }
    }
    if (-not [string]::IsNullOrWhiteSpace($normalized) -and $normalized -ne $Architecture) {
        throw "$Description CMake tree targets $reported, not requested architecture $Architecture`: $Cache"
    }
}

$zanna = Resolve-ZannaExecutable -Tree $toolBuildDir
$targetZanna = Resolve-ZannaExecutable -Tree $buildDir

function Ensure-ZannaBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Tree,
        [Parameter(Mandatory = $true)][string]$Architecture,
        [Parameter(Mandatory = $true)][bool]$TreeIsExplicit,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Write-Host "$Description not found in $Tree"
    $cache = Join-Path $Tree "CMakeCache.txt"
    if (Test-Path -LiteralPath $cache -PathType Leaf) {
        Assert-CMakeTreeArchitecture -Cache $cache -Architecture $Architecture `
            -Description $Description
        Write-Host "Reusing the existing CMake configuration for $Description."
    } else {
        Write-Host "Configuring $Description..."
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
    }
    Write-Host "Building $Description..."
    Invoke-CheckedNative -FilePath "cmake" `
        -Arguments @("--build", $Tree, "--config", $buildType, "--target", "zanna", "-j", [string]$jobs) `
        -FailureMessage "$Description build failed"
    $resolvedExecutable = Resolve-ZannaExecutable -Tree $Tree
    if (-not (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf)) {
        throw "$Description still not found in $Tree"
    }
    return $resolvedExecutable
}

function Copy-DemoAsset {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectDir,
        [Parameter(Mandatory = $true)][string]$SourceRelative,
        [Parameter(Mandatory = $true)][string]$TargetRelative
    )

    if ([IO.Path]::IsPathRooted($SourceRelative) -or [IO.Path]::IsPathRooted($TargetRelative)) {
        throw "Demo asset paths must be relative to their project and examples/bin."
    }
    $source = [IO.Path]::GetFullPath((Join-Path $ProjectDir $SourceRelative))
    if (-not (Test-PathWithin -Base $ProjectDir -Candidate $source -AllowBase)) {
        throw "Asset source escapes the demo project: $SourceRelative"
    }
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Asset not found: $SourceRelative"
    }
    $destination = if ($TargetRelative -eq ".") {
        [IO.Path]::GetFullPath($binDir)
    } else {
        [IO.Path]::GetFullPath((Join-Path $binDir $TargetRelative))
    }
    if (-not (Test-PathWithin -Base $binDir -Candidate $destination -AllowBase)) {
        throw "Asset target escapes the demo output directory: $TargetRelative"
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

function Get-DemoBinRelativePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $binPrefix = [IO.Path]::GetFullPath($binDir)
    $separator = [string][IO.Path]::DirectorySeparatorChar
    if (-not $binPrefix.EndsWith($separator, [StringComparison]::Ordinal)) {
        $binPrefix += $separator
    }
    $candidate = [IO.Path]::GetFullPath($Path)
    if (-not $candidate.StartsWith($binPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the demo directory: $candidate"
    }
    return $candidate.Substring($binPrefix.Length)
}

function Get-DemoBinSnapshot {
    $snapshot = @{}
    foreach ($entry in Get-ChildItem -LiteralPath $binDir -Force -Recurse) {
        $relative = Get-DemoBinRelativePath -Path $entry.FullName
        $snapshot[$relative] = $true
    }
    return $snapshot
}

function Remove-NewDemoArtifacts {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Before,
        [Parameter(Mandatory = $true)][string]$KeepName
    )

    $entries = @(Get-ChildItem -LiteralPath $binDir -Force -Recurse) |
        Sort-Object { $_.FullName.Length } -Descending
    foreach ($entry in $entries) {
        $relative = Get-DemoBinRelativePath -Path $entry.FullName
        if ($Before.ContainsKey($relative) -or
            [string]::Equals($relative, $KeepName, [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        Remove-Item -LiteralPath $entry.FullName -Recurse -Force
    }
}

function Write-DemoRunOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-Item -LiteralPath $Path).Length -eq 0) {
        return
    }
    Write-Host "  ${Label}:"
    foreach ($line in Get-Content -LiteralPath $Path -TotalCount 20) {
        Write-Host "    $line"
    }
}

function Test-DemoRun {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Executable
    )

    $timeoutSeconds = $runTimeoutSeconds
    if ($Name -iin @("3dbowling", "ridgebound", "zannasql", "xenoscape")) {
        $timeoutSeconds = [Math]::Max($timeoutSeconds, 10)
    }

    $before = Get-DemoBinSnapshot
    $temporaryBase = Join-Path ([IO.Path]::GetTempPath()) `
        ("zanna_demo_run_{0}_{1}" -f $Name, [Guid]::NewGuid().ToString("N"))
    $stdoutPath = "$temporaryBase.out"
    $stderrPath = "$temporaryBase.err"
    $process = $null
    $succeeded = $false
    try {
        Write-Host "  Launching for up to $timeoutSeconds second(s)..."
        $process = Start-Process -FilePath $Executable -WorkingDirectory $binDir -PassThru `
            -WindowStyle Hidden -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath
        # Windows PowerShell can lose ExitCode for a redirected process that exits
        # quickly unless the native process handle is materialized before waiting.
        $processHandle = $process.Handle
        if ($processHandle -eq [IntPtr]::Zero) {
            throw "Failed to acquire the demo process handle."
        }
        $exited = $process.WaitForExit($timeoutSeconds * 1000)
        if (-not $exited) {
            if (-not $process.HasExited) {
                try {
                    $process.Kill()
                } catch {
                    if (-not $process.HasExited) {
                        throw
                    }
                }
            }
            $process.WaitForExit()
            Write-Host "  Run smoke: OK (remained active until timeout)"
            $succeeded = $true
        } else {
            $process.WaitForExit()
            if ($process.ExitCode -eq 0) {
                Write-Host "  Run smoke: OK (exit 0)"
                $succeeded = $true
            } else {
                Write-Host "  Run smoke: FAILED (exit $($process.ExitCode))"
                Write-DemoRunOutput -Label "stdout" -Path $stdoutPath
                Write-DemoRunOutput -Label "stderr" -Path $stderrPath
            }
        }
    } catch {
        Write-Host "  Run smoke: FAILED ($($_.Exception.Message))"
        Write-DemoRunOutput -Label "stdout" -Path $stdoutPath
        Write-DemoRunOutput -Label "stderr" -Path $stderrPath
    } finally {
        if ($null -ne $process) {
            $process.Dispose()
        }
        Remove-NewDemoArtifacts -Before $before -KeepName ([IO.Path]::GetFileName($Executable))
        Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    }
    return $succeeded
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
    if ($windowsO0Demos -icontains $Name) {
        if ($Name -ieq "zannasql") {
            Write-Host "  Using -O0 to avoid pathological optimizer/codegen time for this large demo."
        } else {
            Write-Host "  Using -O0 to avoid the Windows native checked-integer optimizer miscompile."
        }
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
        if ($run -and -not (Test-DemoRun -Name $Name -Executable $output)) {
            Write-Host "  FAILED"
            Write-Host "  Finished: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')"
            Write-Host ""
            return $false
        }
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
    if ($run) {
        Write-Host "Run validation: launch from examples/bin with timeout=$runTimeoutSeconds second(s)"
    }
    Write-Host ""

    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Demo manifest not found: $manifest"
    }
    $toolCache = Join-Path $toolBuildDir "CMakeCache.txt"
    if (Test-Path -LiteralPath $toolCache -PathType Leaf) {
        Assert-CMakeTreeArchitecture -Cache $toolCache -Architecture $hostArch `
            -Description "host Zanna tool"
    }
    $targetCache = Join-Path $buildDir "CMakeCache.txt"
    if (Test-Path -LiteralPath $targetCache -PathType Leaf) {
        Assert-CMakeTreeArchitecture -Cache $targetCache -Architecture $demoArch `
            -Description "target-architecture Zanna runtime"
    }
    if (-not (Test-Path -LiteralPath $zanna -PathType Leaf)) {
        $zanna = Ensure-ZannaBuild -Tree $toolBuildDir -Architecture $hostArch `
            -TreeIsExplicit $toolBuildDirExplicit -Description "host Zanna tool"
    }
    if ($toolBuildDir -ine $buildDir -and
        -not (Test-Path -LiteralPath $targetZanna -PathType Leaf)) {
        $targetZanna = Ensure-ZannaBuild -Tree $buildDir -Architecture $demoArch `
            -TreeIsExplicit $buildDirExplicit -Description "target-architecture Zanna runtime"
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
    $seenNames = @{}
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
        if ($name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or
            $directory -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
            Write-Host "ERROR: unsafe demo name or directory in manifest entry: $trimmed"
            ++$failed
            continue
        }
        $nameKey = $name.ToLowerInvariant()
        if ($seenNames.ContainsKey($nameKey)) {
            Write-Host "ERROR: duplicate demo executable name '$name'"
            ++$failed
            continue
        }
        $seenNames[$nameKey] = $true
        if ($category -ieq "games") {
            $categoryRoot = Join-Path $repoRoot "examples\games"
        } elseif ($category -ieq "apps") {
            $categoryRoot = Join-Path $repoRoot "examples\apps"
        } else {
            Write-Host "ERROR: invalid demo category '$category' for '$name'"
            ++$failed
            continue
        }
        $projectDir = [IO.Path]::GetFullPath((Join-Path $categoryRoot $directory))
        if (-not (Test-PathWithin -Base $categoryRoot -Candidate $projectDir)) {
            Write-Host "ERROR: demo directory escapes its category for '$name'"
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
        if ($run) {
            Write-Host "All $succeeded demos built and passed launch smoke validation."
        } else {
            Write-Host "All $succeeded demos built successfully."
        }
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
