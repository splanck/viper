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

    $classesRoot = "HKCU:\Software\Classes"
    if ($InstallRoot.StartsWith($env:ProgramFiles, [System.StringComparison]::OrdinalIgnoreCase)) {
        $classesRoot = "HKLM:\Software\Classes"
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
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
