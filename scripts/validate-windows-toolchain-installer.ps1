# SPDX-License-Identifier: GPL-3.0-only
# Validate a generated Viper Windows toolchain installer on a clean Windows VM.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Installer,

    [string]$InstallRoot = "$env:LOCALAPPDATA\Viper",

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
$work = Join-Path $env:TEMP ("viper-installer-vm-" + [Guid]::NewGuid().ToString("N"))
$powershell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
$originalPath = $env:Path
New-Item -ItemType Directory -Path $work | Out-Null

try {
    if (Test-Path -LiteralPath (Join-Path $InstallRoot "uninstall.exe")) {
        Run-Checked -FilePath (Join-Path $InstallRoot "uninstall.exe") -Arguments @("/quiet", "/norestart") | Out-Null
    }

    Run-Checked -FilePath $installerPath -Arguments @("/quiet", "/norestart") | Out-Null

    $viper = Join-Path $InstallRoot "bin\viper.exe"
    if (-not (Test-Path -LiteralPath $viper -PathType Leaf)) {
        throw "Expected installed viper.exe at $viper"
    }

    Run-Checked -FilePath $viper -Arguments @("--version") | Out-Host

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
il 0.2.0

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
}
finally {
    if (-not $KeepInstalled -and (Test-Path -LiteralPath (Join-Path $InstallRoot "uninstall.exe"))) {
        $uninstaller = Join-Path $InstallRoot "uninstall.exe"
        Run-Checked -FilePath $uninstaller -Arguments @("/quiet", "/norestart") | Out-Null
        Remove-Item -LiteralPath $uninstaller -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $InstallRoot -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
