#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/validate-windows-toolchain-installer.ps1
# Purpose: Validate a native Zanna toolchain installer on a disposable Windows host.
#
# Key invariants:
#   - Package identity and paths come from the recursively verified /inspect record.
#   - Existing installations are never removed without an explicit opt-in.
#   - Validation finishes through the product uninstaller and checks detached cleanup.
#
# Ownership/Lifetime: A GUID-named temporary workspace and this run's product state.
#
# Links: docs/installer-release.md, WindowsToolchainInstallerE2E.cmake
#
#===----------------------------------------------------------------------===#

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Installer,

    [string]$BaselineInstaller = "",
    [string]$UpgradeStaleRelativePath = "share\zanna\installer-upgrade-stale.txt",
    [string]$InstallRoot = "",
    [string]$Scope = "",
    [string]$SignToolPath = "signtool.exe",

    [switch]$RequireSignature,
    [switch]$ReplaceExisting,
    [switch]$KeepInstalled
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Quote-ProcessArgument {
    param([AllowNull()][string]$Argument)

    if ($null -eq $Argument) {
        return '""'
    }
    $escaped = $Argument -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    if ($escaped.Length -eq 0 -or $escaped -match '[\s"]') {
        return '"' + $escaped + '"'
    }
    return $escaped
}

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = (Get-Location).Path
    )

    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    if ($psi.PSObject.Properties.Name -contains "ArgumentList") {
        foreach ($argument in $Arguments) {
            [void]$psi.ArgumentList.Add($argument)
        }
    } else {
        $psi.Arguments = ($Arguments | ForEach-Object { Quote-ProcessArgument $_ }) -join " "
    }
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($psi)
    if ($null -eq $process) {
        throw "Failed to start process: $FilePath"
    }
    try {
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Stdout = $stdoutTask.GetAwaiter().GetResult()
            Stderr = $stderrTask.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}

function Invoke-CheckedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = (Get-Location).Path,
        [int[]]$SuccessCodes = @(0)
    )

    $result = Invoke-CapturedProcess -FilePath $FilePath -Arguments $Arguments `
        -WorkingDirectory $WorkingDirectory
    if ($result.ExitCode -notin $SuccessCodes) {
        throw "$FilePath failed with exit code $($result.ExitCode)`n" +
            "stdout:`n$($result.Stdout)`nstderr:`n$($result.Stderr)"
    }
    return $result.Stdout
}

function Get-InstallerMetadata {
    param([Parameter(Mandatory = $true)][string]$Path)

    $jsonPath = Join-Path ([IO.Path]::GetTempPath()) `
        ("zanna-installer-inspect-{0}.json" -f [Guid]::NewGuid().ToString("N"))
    try {
        $result = Invoke-CapturedProcess -FilePath $Path `
            -Arguments @("/inspect", "/output", $jsonPath)
        if ($result.ExitCode -ne 0) {
            throw "Installer inspection failed for $Path`n$($result.Stdout)`n$($result.Stderr)"
        }
        if (-not (Test-Path -LiteralPath $jsonPath -PathType Leaf)) {
            throw "Installer inspection did not create $jsonPath."
        }
        $json = [IO.File]::ReadAllText($jsonPath, [Text.Encoding]::UTF8)
        $metadata = $json | ConvertFrom-Json
    } catch {
        throw "Installer /inspect did not return valid JSON for $Path`: $($_.Exception.Message)"
    } finally {
        Remove-Item -LiteralPath $jsonPath -Force -ErrorAction SilentlyContinue
    }
    $requiredFields = @(
        "schema_version", "kind", "identifier", "display_name", "version",
        "architecture", "channel", "default_scope", "default_install_dir", "components"
    )
    foreach ($field in $requiredFields) {
        if ($metadata.PSObject.Properties.Name -notcontains $field) {
            throw "Installer /inspect is missing required schema-3 field '$field'."
        }
    }
    if ($metadata.schema_version -lt 3 -or $metadata.kind -ne "toolchain" -or
        [string]::IsNullOrWhiteSpace($metadata.identifier) -or
        [string]::IsNullOrWhiteSpace($metadata.default_install_dir)) {
        throw "Installer does not expose the required native schema-3 toolchain identity."
    }
    return $metadata
}

function Wait-PathAbsent {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$Seconds = 30
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline -and (Test-Path -LiteralPath $Path)) {
        Start-Sleep -Milliseconds 100
    }
    return -not (Test-Path -LiteralPath $Path)
}

function Test-PathEntry {
    param([AllowNull()][string]$Value, [Parameter(Mandatory = $true)][string]$Expected)

    foreach ($entry in @($Value -split ";")) {
        if ($entry -and [string]::Equals(
                $entry.Trim().TrimEnd('\'),
                $Expected.TrimEnd('\'),
                [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Assert-AssociationState {
    param(
        [Parameter(Mandatory = $true)][string]$ClassesRoot,
        [Parameter(Mandatory = $true)][string]$Identifier,
        [Parameter(Mandatory = $true)][bool]$Expected
    )

    foreach ($extension in @("zia", "bas", "il")) {
        $progId = "$Identifier.$extension"
        $openWith = Join-Path $ClassesRoot ".$extension\OpenWithProgids"
        $present = $false
        if (Test-Path -LiteralPath $openWith) {
            $properties = Get-ItemProperty -LiteralPath $openWith
            $present = $properties.PSObject.Properties.Name -contains $progId
        }
        if ($present -ne $Expected) {
            throw "Open With state for $progId was $present; expected $Expected."
        }
        $progIdPath = Join-Path $ClassesRoot $progId
        if ((Test-Path -LiteralPath $progIdPath) -ne $Expected) {
            throw "ProgID state for $progIdPath did not match expected state $Expected."
        }
    }
}

function Assert-AdministratorForMachineScope {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Machine-scope validation must run from an elevated PowerShell session."
    }
}

$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$metadata = Get-InstallerMetadata $installerPath
$baselinePath = $null
if (-not [string]::IsNullOrWhiteSpace($BaselineInstaller)) {
    $baselinePath = (Resolve-Path -LiteralPath $BaselineInstaller).Path
    $baselineMetadata = Get-InstallerMetadata $baselinePath
    if ($baselineMetadata.identifier -ne $metadata.identifier -or
        $baselineMetadata.architecture -ne $metadata.architecture) {
        throw "Baseline and current installers must have the same identifier and architecture."
    }
}

if ([string]::IsNullOrWhiteSpace($Scope)) {
    $Scope = [string]$metadata.default_scope
}
$Scope = $Scope.ToLowerInvariant()
if ($Scope -notin @("user", "machine")) {
    throw "Scope must be user or machine."
}
if ($Scope -eq "machine") {
    Assert-AdministratorForMachineScope
}

if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    $base = if ($Scope -eq "machine") {
        $env:ProgramFiles
    } else {
        Join-Path $env:LOCALAPPDATA "Programs"
    }
    $InstallRoot = Join-Path $base ([string]$metadata.default_install_dir)
}
$InstallRoot = [IO.Path]::GetFullPath($InstallRoot)
if ($InstallRoot -eq [IO.Path]::GetPathRoot($InstallRoot) -or $InstallRoot.Length -lt 8) {
    throw "Refusing unsafe validation install root: $InstallRoot"
}

$identifier = [string]$metadata.identifier
$machineScope = $Scope -eq "machine"
$classesRoot = if ($machineScope) { "HKLM:\Software\Classes" } else { "HKCU:\Software\Classes" }
$uninstallRegistry = if ($machineScope) {
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$identifier"
} else {
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$identifier"
}
$pathScope = if ($machineScope) { "Machine" } else { "User" }
$installedPathEntry = Join-Path $InstallRoot "bin"
$startMenuBase = if ($machineScope) {
    Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs"
} else {
    Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
}
$startMenu = Join-Path $startMenuBase ([string]$metadata.default_install_dir)
$work = Join-Path $env:TEMP ("zanna-installer-vm-" + [Guid]::NewGuid().ToString("N"))
$powershell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
$originalPath = $env:Path
$unownedSentinel = Join-Path $InstallRoot "validator-unowned-sentinel.txt"
$stalePath = if ($UpgradeStaleRelativePath) {
    Join-Path $InstallRoot $UpgradeStaleRelativePath
} else {
    $null
}
$installedByValidation = $false
$maintenanceCache = $null
$components = @($metadata.components)
New-Item -ItemType Directory -Path $work -Force | Out-Null

try {
    $hostArchitecture = if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") { "arm64" } else { "x64" }
    if ($metadata.architecture -ne $hostArchitecture) {
        throw "Package architecture $($metadata.architecture) does not match validation host $hostArchitecture."
    }

    if ($RequireSignature) {
        Invoke-CheckedProcess -FilePath $SignToolPath `
            -Arguments @("verify", "/pa", "/all", "/tw", "/v", $installerPath) | Out-Null
    }

    $existingProduct = Test-Path -LiteralPath $uninstallRegistry
    if ($existingProduct -and -not $ReplaceExisting) {
        throw "A product with identifier $identifier is already installed. Use a disposable host " +
            "or pass -ReplaceExisting to remove it explicitly."
    }
    if ($existingProduct) {
        $existing = Get-ItemProperty -LiteralPath $uninstallRegistry
        $existingMaintenance = if ($existing.PSObject.Properties.Name -contains
            "ZannaMaintenanceCache") {
            [string]$existing.ZannaMaintenanceCache
        } else {
            ""
        }
        if ([string]::IsNullOrWhiteSpace($existingMaintenance) -or
            -not (Test-Path -LiteralPath $existingMaintenance -PathType Leaf)) {
            throw "Existing product has no verified maintenance cache: $identifier"
        }
        Invoke-CheckedProcess -FilePath $existingMaintenance `
            -Arguments @("/uninstall", "/quiet", "/norestart") -SuccessCodes @(0, 3010) | Out-Null
        if (-not (Wait-PathAbsent -Path $uninstallRegistry)) {
            throw "Existing product registration did not disappear after uninstall."
        }
    }

    $installArguments = @(
        "/install", "/quiet", "/norestart", "/scope", $Scope,
        "/installDir", $InstallRoot, "/type", "complete",
        "/addToPath", "/associations", "/shortcuts"
    )
    if ($baselinePath) {
        Invoke-CheckedProcess -FilePath $baselinePath -Arguments $installArguments `
            -SuccessCodes @(0, 3010) | Out-Null
        $installedByValidation = $true
        if ($stalePath -and -not (Test-Path -LiteralPath $stalePath -PathType Leaf)) {
            throw "Baseline installer omitted the expected stale-owned file: $stalePath"
        }
        New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
        [IO.File]::WriteAllText($unownedSentinel, "preserve-unowned-upgrade-content")
    }

    Invoke-CheckedProcess -FilePath $installerPath -Arguments $installArguments `
        -SuccessCodes @(0, 3010) | Out-Null
    $installedByValidation = $true

    if ($stalePath -and $baselinePath -and (Test-Path -LiteralPath $stalePath)) {
        throw "Upgrade left a file owned only by the baseline package: $stalePath"
    }
    if ($baselinePath -and
        [IO.File]::ReadAllText($unownedSentinel) -ne "preserve-unowned-upgrade-content") {
        throw "Upgrade modified unrelated content in the install tree."
    }

    if (-not (Test-Path -LiteralPath $uninstallRegistry)) {
        throw "Expected Apps & Features registration at $uninstallRegistry"
    }
    $arp = Get-ItemProperty -LiteralPath $uninstallRegistry
    foreach ($requiredProperty in @(
            "DisplayName", "DisplayVersion", "Publisher", "InstallLocation",
            "UninstallString", "QuietUninstallString", "ModifyPath", "RepairPath",
            "EstimatedSize", "ZannaMaintenanceCache")) {
        if ($arp.PSObject.Properties.Name -notcontains $requiredProperty -or
            [string]::IsNullOrWhiteSpace([string]$arp.$requiredProperty)) {
            throw "Apps & Features metadata is missing $requiredProperty."
        }
    }
    if (-not [string]::Equals(
            [IO.Path]::GetFullPath([string]$arp.InstallLocation).TrimEnd('\'),
            $InstallRoot.TrimEnd('\'),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Apps & Features InstallLocation does not match the selected root."
    }
    $maintenanceCache = [string]$arp.ZannaMaintenanceCache
    if (-not (Test-Path -LiteralPath $maintenanceCache -PathType Leaf)) {
        throw "Verified maintenance cache is missing: $maintenanceCache"
    }

    $requiredTools = @(
        "zanna", "zia", "vbasic", "ilrun", "il-verify", "il-dis",
        "zia-server", "vbasic-server", "basic-ast-dump", "basic-lex-dump"
    )
    if ($components -contains "zannaide") {
        $requiredTools += "zannaide"
    }
    foreach ($tool in $requiredTools) {
        $toolPath = Join-Path $InstallRoot "bin\$tool.exe"
        if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
            throw "Expected installed tool: $toolPath"
        }
    }

    $developerPrompt = Join-Path $InstallRoot "bin\zanna-dev.cmd"
    if (-not (Test-Path -LiteralPath $developerPrompt -PathType Leaf)) {
        throw "Expected installed developer prompt: $developerPrompt"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $startMenu "Zanna Developer Prompt.lnk"))) {
        throw "Expected developer prompt Start Menu shortcut under $startMenu"
    }
    if ($components -contains "zannaide" -and
        -not (Test-Path -LiteralPath (Join-Path $startMenu "ZannaIDE.lnk"))) {
        throw "Expected ZannaIDE Start Menu shortcut under $startMenu"
    }

    if (-not (Test-PathEntry `
            -Value ([Environment]::GetEnvironmentVariable("Path", $pathScope)) `
            -Expected $installedPathEntry)) {
        throw "Installer did not add its selected PATH entry: $installedPathEntry"
    }
    Assert-AssociationState -ClassesRoot $classesRoot -Identifier $identifier -Expected $true

    if ($components.Count -gt 1) {
        Invoke-CheckedProcess -FilePath $maintenanceCache -Arguments @(
            "/modify", "/quiet", "/norestart", "/type", "minimal",
            "/noPath", "/noAssociations", "/noShortcuts") -SuccessCodes @(0, 3010) | Out-Null
        if ($components -contains "zannaide" -and
            (Test-Path -LiteralPath (Join-Path $InstallRoot "bin\zannaide.exe"))) {
            throw "Minimal modify left the ZannaIDE component installed."
        }
        if (Test-PathEntry `
                -Value ([Environment]::GetEnvironmentVariable("Path", $pathScope)) `
                -Expected $installedPathEntry) {
            throw "Minimal modify left an integration that was disabled: PATH"
        }
        Assert-AssociationState -ClassesRoot $classesRoot -Identifier $identifier -Expected $false

        Invoke-CheckedProcess -FilePath $maintenanceCache -Arguments @(
            "/modify", "/quiet", "/norestart", "/type", "complete",
            "/addToPath", "/associations", "/shortcuts") -SuccessCodes @(0, 3010) | Out-Null
        Assert-AssociationState -ClassesRoot $classesRoot -Identifier $identifier -Expected $true
    }

    $zanna = Join-Path $InstallRoot "bin\zanna.exe"
    $expectedZannaHash = (Get-FileHash -LiteralPath $zanna -Algorithm SHA256).Hash
    if (-not (Test-Path -LiteralPath $unownedSentinel)) {
        [IO.File]::WriteAllText($unownedSentinel, "preserve-unowned-repair-content")
    }
    [IO.File]::WriteAllText($zanna, "intentionally-corrupt-for-repair")
    Invoke-CheckedProcess -FilePath $maintenanceCache `
        -Arguments @("/repair", "/quiet", "/norestart") -SuccessCodes @(0, 3010) | Out-Null
    if ((Get-FileHash -LiteralPath $zanna -Algorithm SHA256).Hash -ne $expectedZannaHash) {
        throw "Repair did not restore the exact owned zanna.exe bytes."
    }
    if (-not (Test-Path -LiteralPath $unownedSentinel -PathType Leaf)) {
        throw "Repair removed unrelated developer content."
    }

    if ($RequireSignature) {
        Invoke-CheckedProcess -FilePath $SignToolPath `
            -Arguments @("verify", "/pa", "/all", "/tw", "/v", $maintenanceCache) | Out-Null
        foreach ($pe in Get-ChildItem -LiteralPath (Join-Path $InstallRoot "bin") -File |
                Where-Object { $_.Extension -in @(".exe", ".dll") }) {
            Invoke-CheckedProcess -FilePath $SignToolPath `
                -Arguments @("verify", "/pa", "/all", "/tw", "/v", $pe.FullName) | Out-Null
        }
    }

    Invoke-CheckedProcess -FilePath $zanna -Arguments @("--version") | Out-Host
    $pathFromRegistry = ([Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
                         [Environment]::GetEnvironmentVariable("Path", "User"))
    $env:Path = $pathFromRegistry
    try {
        Invoke-CheckedProcess -FilePath $powershell -Arguments @(
            "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
            "-Command", "zanna --version") | Out-Host
    } finally {
        $env:Path = $originalPath
    }

    $basic = Join-Path $work "installer-run-smoke.bas"
    [IO.File]::WriteAllText($basic, '10 PRINT "INSTALLER-RUN-SMOKE"')
    $runOut = Invoke-CheckedProcess -FilePath $zanna -Arguments @("run", $basic)
    if ($runOut -notmatch "INSTALLER-RUN-SMOKE") {
        throw "zanna run produced unexpected output: $runOut"
    }

    $il = Join-Path $work "installer-native-smoke.il"
    $nativeExe = Join-Path $work "installer-native-smoke.exe"
    Remove-Item Env:ZANNA_LIB_PATH -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText($il, @'
il 0.3.0

extern @Zanna.Terminal.PrintStr(str) -> void
global const str @.msg = "INSTALLER-NATIVE-SMOKE"

func @main() -> i64 {
entry:
  %msg = const_str @.msg
  call @Zanna.Terminal.PrintStr(%msg)
  ret 0
}
'@)
    Invoke-CheckedProcess -FilePath $zanna -Arguments @(
        "codegen", [string]$metadata.architecture, $il, "-o", $nativeExe) `
        -WorkingDirectory $work | Out-Null
    $nativeOut = Invoke-CheckedProcess -FilePath $nativeExe -WorkingDirectory $work
    if ($nativeOut -notmatch "INSTALLER-NATIVE-SMOKE") {
        throw "Native smoke produced unexpected output: $nativeOut"
    }

    if ($components -contains "sdk") {
        $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
        $cmd = Join-Path $env:SystemRoot "System32\cmd.exe"
        $consumerSource = Join-Path $work "cmake-consumer-source"
        $consumerBuild = Join-Path $work "cmake-consumer-build"
        New-Item -ItemType Directory -Path $consumerSource -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $consumerSource "CMakeLists.txt"), @'
cmake_minimum_required(VERSION 3.20)
project(zanna_installer_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Zanna CONFIG REQUIRED)
add_executable(zanna_installer_consumer main.cpp)
target_link_libraries(zanna_installer_consumer PRIVATE zanna::il_core zanna::il_io)
'@)
        [IO.File]::WriteAllText((Join-Path $consumerSource "main.cpp"), @'
#include <sstream>
#include <zanna/il/core/Module.hpp>
#include <zanna/il/io/Serializer.hpp>

int main() {
    il::core::Module module;
    std::ostringstream out;
    il::io::Serializer::write(module, out);
    return out.str().empty() ? 1 : 0;
}
'@)
        $consumerDriver = Join-Path $work "build-cmake-consumer.cmd"
        [IO.File]::WriteAllText($consumerDriver, @"
@echo off
chcp 65001 >nul
call "$developerPrompt"
if errorlevel 1 exit /b %errorlevel%
"$cmake" -S "$consumerSource" -B "$consumerBuild"
if errorlevel 1 exit /b %errorlevel%
"$cmake" --build "$consumerBuild" --config Release
exit /b %errorlevel%
"@)
        Invoke-CheckedProcess -FilePath $cmd -Arguments @("/d", "/c", $consumerDriver) | Out-Host
        $consumerExe = @(
            (Join-Path $consumerBuild "Release\zanna_installer_consumer.exe"),
            (Join-Path $consumerBuild "zanna_installer_consumer.exe")
        ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if (-not $consumerExe) {
            throw "CMake consumer build did not produce zanna_installer_consumer.exe."
        }
        Invoke-CheckedProcess -FilePath $consumerExe | Out-Null
    }

    Write-Host "Windows installer validation passed for $($metadata.display_name) " `
        "$($metadata.version) $($metadata.architecture)."
} finally {
    $env:Path = $originalPath
    if (-not $KeepInstalled -and $installedByValidation -and
        (Test-Path -LiteralPath $uninstallRegistry)) {
        $rootUninstaller = Join-Path $InstallRoot "uninstall.exe"
        $cleanupExecutable = if (Test-Path -LiteralPath $rootUninstaller -PathType Leaf) {
            $rootUninstaller
        } elseif ($maintenanceCache -and
            (Test-Path -LiteralPath $maintenanceCache -PathType Leaf)) {
            $maintenanceCache
        } else {
            throw "Installed product has no available uninstaller."
        }
        Invoke-CheckedProcess -FilePath $cleanupExecutable `
            -Arguments @("/uninstall", "/quiet", "/norestart") -SuccessCodes @(0, 3010) | Out-Null
        if (-not (Wait-PathAbsent -Path $uninstallRegistry)) {
            throw "Uninstaller left Apps & Features registration: $uninstallRegistry"
        }
        if ($maintenanceCache -and -not (Wait-PathAbsent -Path $maintenanceCache)) {
            throw "Detached cleanup left the maintenance cache: $maintenanceCache"
        }
        foreach ($ownedPath in @(
                (Join-Path $InstallRoot "bin\zanna.exe"),
                (Join-Path $InstallRoot "uninstall.exe"),
                (Join-Path $InstallRoot ".zanna-install-manifest.txt"))) {
            if (Test-Path -LiteralPath $ownedPath) {
                throw "Uninstaller left an owned path: $ownedPath"
            }
        }
        Assert-AssociationState -ClassesRoot $classesRoot -Identifier $identifier -Expected $false
        if (Test-PathEntry `
                -Value ([Environment]::GetEnvironmentVariable("Path", $pathScope)) `
                -Expected $installedPathEntry) {
            throw "Uninstaller left its PATH entry: $installedPathEntry"
        }
        if (Test-Path -LiteralPath $startMenu) {
            throw "Uninstaller left its Start Menu directory: $startMenu"
        }
        if (Test-Path -LiteralPath $unownedSentinel -PathType Leaf) {
            $sentinelText = [IO.File]::ReadAllText($unownedSentinel)
            if ($sentinelText -notmatch '^preserve-unowned-') {
                throw "Uninstaller modified unrelated content: $unownedSentinel"
            }
            Remove-Item -LiteralPath $unownedSentinel -Force
        }
        if (Test-Path -LiteralPath $InstallRoot) {
            $remaining = @(Get-ChildItem -LiteralPath $InstallRoot -Force -Recurse)
            if ($remaining.Count -ne 0) {
                throw "Uninstaller left unexpected content under $InstallRoot`: " +
                    (($remaining | Select-Object -ExpandProperty FullName) -join ", ")
            }
            Remove-Item -LiteralPath $InstallRoot -Force
        }
    }
    if (Test-Path -LiteralPath $work) {
        $resolvedWork = (Resolve-Path -LiteralPath $work).Path
        if (-not $resolvedWork.StartsWith(
                [IO.Path]::GetFullPath($env:TEMP), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean unexpected validation workspace: $resolvedWork"
        }
        Remove-Item -LiteralPath $resolvedWork -Recurse -Force
    }
}
