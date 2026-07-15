# SPDX-License-Identifier: GPL-3.0-only
# Validate a generated Viper Windows toolchain installer on a clean Windows VM.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Installer,

    [string]$BaselineInstaller,
    [string]$UpgradeStaleRelativePath = "share\viper\installer-upgrade-stale.txt",

    [string]$InstallRoot = "$env:LOCALAPPDATA\Viper",
    [string]$SignToolPath = "signtool.exe",

    [switch]$RequireSignature,
    [switch]$KeepInstalled
)

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

function Run-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = (Get-Location).Path
    )

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    if ($psi.PSObject.Properties.Name -contains "ArgumentList") {
        foreach ($arg in $Arguments) {
            [void]$psi.ArgumentList.Add($arg)
        }
    } else {
        $psi.Arguments = ($Arguments | ForEach-Object { Quote-ProcessArgument $_ }) -join " "
    }
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($psi)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "$FilePath failed with exit code $($process.ExitCode)`nstdout:`n$stdout`nstderr:`n$stderr"
    }
    return $stdout
}

$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$baselineInstallerPath = $null
if ($BaselineInstaller) {
    $baselineInstallerPath = (Resolve-Path -LiteralPath $BaselineInstaller).Path
}
$work = Join-Path $env:TEMP ("viper-installer-vm-" + [Guid]::NewGuid().ToString("N"))
$powershell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
$originalPath = $env:Path
$upgradeUnrelated = Join-Path $InstallRoot "share\viper\installer-upgrade-unrelated.txt"
$upgradeUnrelatedExpected = $false
$machineScope = $InstallRoot.StartsWith(
    $env:ProgramFiles, [System.StringComparison]::OrdinalIgnoreCase)
$classesRoot = if ($machineScope) { "HKLM:\Software\Classes" } else { "HKCU:\Software\Classes" }
$uninstallRegistry = if ($machineScope) {
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\org.viper.toolchain"
} else {
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\org.viper.toolchain"
}
$pathScope = if ($machineScope) { "Machine" } else { "User" }
$installedPathEntry = Join-Path $InstallRoot "bin"
New-Item -ItemType Directory -Path $work | Out-Null

try {
    if ($RequireSignature) {
        & $SignToolPath verify /pa /all $installerPath
        if ($LASTEXITCODE -ne 0) {
            throw "signtool verify failed for signed installer: $installerPath"
        }
    }

    if (Test-Path -LiteralPath (Join-Path $InstallRoot "uninstall.exe")) {
        Run-Checked -FilePath (Join-Path $InstallRoot "uninstall.exe") -Arguments @("/quiet", "/norestart") | Out-Null
    }

    if ($baselineInstallerPath) {
        Run-Checked -FilePath $baselineInstallerPath -Arguments @("/quiet", "/norestart") | Out-Null
        if ($UpgradeStaleRelativePath) {
            $stalePath = Join-Path $InstallRoot $UpgradeStaleRelativePath
            if (-not (Test-Path -LiteralPath $stalePath -PathType Leaf)) {
                throw "Baseline installer did not install the expected upgrade-stale file: $stalePath"
            }
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $upgradeUnrelated) -Force | Out-Null
        Set-Content -LiteralPath $upgradeUnrelated -Value "preserve-unowned-upgrade-content" -Encoding ASCII
        $upgradeUnrelatedExpected = $true
    }

    Run-Checked -FilePath $installerPath -Arguments @("/quiet", "/norestart") | Out-Null

    if ($baselineInstallerPath) {
        if ($UpgradeStaleRelativePath) {
            $stalePath = Join-Path $InstallRoot $UpgradeStaleRelativePath
            if (Test-Path -LiteralPath $stalePath) {
                throw "Upgrade left a stale file owned only by the baseline package: $stalePath"
            }
        }
        if ((Get-Content -LiteralPath $upgradeUnrelated -Raw).Trim() -ne
            "preserve-unowned-upgrade-content") {
            throw "Upgrade modified unrelated content in the install tree: $upgradeUnrelated"
        }
    }

    $requiredTools = @(
        "viper",
        "zia",
        "vbasic",
        "ilrun",
        "il-verify",
        "il-dis",
        "zia-server",
        "vbasic-server",
        "basic-ast-dump",
        "basic-lex-dump",
        "viperide"
    )
    foreach ($tool in $requiredTools) {
        $toolPath = Join-Path $InstallRoot "bin\$tool.exe"
        if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
            throw "Expected installed $tool.exe at $toolPath"
        }
    }

    $viper = Join-Path $InstallRoot "bin\viper.exe"
    $viperide = Join-Path $InstallRoot "bin\viperide.exe"
    $viperIcon = Join-Path $InstallRoot "share\viper\viper.ico"
    if (-not (Test-Path -LiteralPath $viperIcon -PathType Leaf)) {
        throw "Expected installed Viper icon at $viperIcon"
    }
    Run-Checked -FilePath $viper -Arguments @("--version") | Out-Host
    Run-Checked -FilePath $viperide -Arguments @("--version") | Out-Host

    if (-not (Test-Path -LiteralPath $uninstallRegistry)) {
        throw "Expected Add/Remove Programs registration at $uninstallRegistry"
    }
    $ziaAssoc = Join-Path $classesRoot ".zia\OpenWithProgids"
    if (-not (Test-Path -LiteralPath $ziaAssoc)) {
        throw "Expected .zia OpenWithProgids registration at $ziaAssoc"
    }
    $ziaProps = Get-ItemProperty -LiteralPath $ziaAssoc
    if ($ziaProps.PSObject.Properties.Name -notcontains "org.viper.toolchain.zia") {
        throw "Expected .zia to advertise org.viper.toolchain.zia"
    }

    $pathFromRegistry = ([Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
                         [Environment]::GetEnvironmentVariable("Path", "User"))
    $env:Path = $pathFromRegistry
    try {
        Run-Checked -FilePath $powershell -Arguments @(
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            "viper --version"
        ) | Out-Host
    } finally {
        $env:Path = $originalPath
    }

    $basic = Join-Path $work "installer-run-smoke.bas"
    Set-Content -LiteralPath $basic -Value '10 PRINT "INSTALLER-RUN-SMOKE"' -Encoding ASCII
    $runOut = Run-Checked -FilePath $viper -Arguments @("run", $basic)
    if ($runOut -notmatch "INSTALLER-RUN-SMOKE") {
        throw "viper run produced unexpected output: $runOut"
    }

    $arch = if ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") { "arm64" } else { "x64" }
    $il = Join-Path $work "installer-native-smoke.il"
    $exe = Join-Path $work "installer-native-smoke.exe"
    Remove-Item Env:VIPER_LIB_PATH -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $il -Encoding ASCII -Value @"
il 0.3.0

extern @Viper.Terminal.PrintStr(str) -> void
global const str @.msg = "INSTALLER-NATIVE-SMOKE"

func @main() -> i64 {
entry:
  %msg = const_str @.msg
  call @Viper.Terminal.PrintStr(%msg)
  ret 0
}
"@
    Run-Checked -FilePath $viper -Arguments @("codegen", $arch, $il, "-o", $exe) -WorkingDirectory $work | Out-Null
    $nativeOut = Run-Checked -FilePath $exe -WorkingDirectory $work
    if ($nativeOut -notmatch "INSTALLER-NATIVE-SMOKE") {
        throw "native smoke produced unexpected output: $nativeOut"
    }

    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    $cmd = Join-Path $env:SystemRoot "System32\cmd.exe"
    $developerPrompt = Join-Path $InstallRoot "bin\viper-dev.cmd"
    if (-not (Test-Path -LiteralPath $developerPrompt -PathType Leaf)) {
        throw "Expected installed developer prompt at $developerPrompt"
    }
    $consumerSource = Join-Path $work "cmake-consumer-source"
    $consumerBuild = Join-Path $work "cmake-consumer-build"
    New-Item -ItemType Directory -Path $consumerSource | Out-Null
    Set-Content -LiteralPath (Join-Path $consumerSource "CMakeLists.txt") -Encoding ASCII -Value @'
cmake_minimum_required(VERSION 3.20)
project(viper_installer_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Viper CONFIG REQUIRED)
add_executable(viper_installer_consumer main.cpp)
target_link_libraries(viper_installer_consumer PRIVATE viper::il_core viper::il_io)
'@
    Set-Content -LiteralPath (Join-Path $consumerSource "main.cpp") -Encoding ASCII -Value @'
#include <sstream>
#include <viper/il/core/Module.hpp>
#include <viper/il/io/Serializer.hpp>

int main() {
    il::core::Module module;
    std::ostringstream out;
    il::io::Serializer::write(module, out);
    return out.str().empty() ? 1 : 0;
}
'@
    $consumerDriver = Join-Path $work "build-cmake-consumer.cmd"
    Set-Content -LiteralPath $consumerDriver -Encoding ASCII -Value @"
@echo off
call "$developerPrompt"
if errorlevel 1 exit /b %errorlevel%
"$cmake" -S "$consumerSource" -B "$consumerBuild"
if errorlevel 1 exit /b %errorlevel%
"$cmake" --build "$consumerBuild" --config Release
exit /b %errorlevel%
"@
    Run-Checked -FilePath $cmd -Arguments @("/d", "/c", $consumerDriver) | Out-Host
    $consumerExe = @(
        (Join-Path $consumerBuild "Release\viper_installer_consumer.exe"),
        (Join-Path $consumerBuild "viper_installer_consumer.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $consumerExe) {
        throw "CMake consumer build did not produce viper_installer_consumer.exe"
    }
    Run-Checked -FilePath $consumerExe | Out-Null
}
finally {
    if (-not $KeepInstalled -and (Test-Path -LiteralPath (Join-Path $InstallRoot "uninstall.exe"))) {
        $uninstaller = Join-Path $InstallRoot "uninstall.exe"
        Run-Checked -FilePath $uninstaller -Arguments @("/quiet", "/norestart") | Out-Null
        Remove-Item -LiteralPath $uninstaller -Force -ErrorAction SilentlyContinue
        if ($upgradeUnrelatedExpected) {
            if (-not (Test-Path -LiteralPath $upgradeUnrelated -PathType Leaf)) {
                throw "Uninstaller removed unrelated upgrade-test content: $upgradeUnrelated"
            }
            Remove-Item -LiteralPath $upgradeUnrelated -Force
        }
        foreach ($ownedPath in @(
            (Join-Path $InstallRoot "bin\viper.exe"),
            (Join-Path $InstallRoot "share\viper\.viper-install-manifest.txt"),
            (Join-Path $InstallRoot ".viper-install-manifest.txt")
        )) {
            if (Test-Path -LiteralPath $ownedPath) {
                throw "Uninstaller left an owned path: $ownedPath"
            }
        }
        Get-ChildItem -LiteralPath $InstallRoot -Directory -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Remove-Item -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $InstallRoot -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $InstallRoot) {
            throw "Uninstaller left unexpected content under $InstallRoot"
        }
        if (Test-Path -LiteralPath $uninstallRegistry) {
            throw "Uninstaller left Add/Remove Programs registration at $uninstallRegistry"
        }
        foreach ($extension in @(".zia", ".bas", ".il")) {
            $openWith = Join-Path $classesRoot "$extension\OpenWithProgids"
            if (Test-Path -LiteralPath $openWith) {
                $props = Get-ItemProperty -LiteralPath $openWith
                if ($props.PSObject.Properties.Name -contains "org.viper.toolchain$extension") {
                    throw "Uninstaller left Viper association under $openWith"
                }
            }
            $progId = Join-Path $classesRoot "org.viper.toolchain$extension"
            if (Test-Path -LiteralPath $progId) {
                throw "Uninstaller left Viper ProgID at $progId"
            }
        }
        $remainingPath = [Environment]::GetEnvironmentVariable("Path", $pathScope)
        $pathEntries = @($remainingPath -split ";" | Where-Object { $_.Length -gt 0 })
        if ($pathEntries | Where-Object {
                [string]::Equals(
                    $_, $installedPathEntry, [System.StringComparison]::OrdinalIgnoreCase)
            }) {
            throw "Uninstaller left its PATH entry: $installedPathEntry"
        }
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
