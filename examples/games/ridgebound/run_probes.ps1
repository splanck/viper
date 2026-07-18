#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: examples/games/ridgebound/run_probes.ps1
# Purpose: Run every Ridgebound release gate with an existing Zanna binary.
# Key invariants:
#   - The script never configures, builds, or invokes CTest.
#   - A probe passes only after a clean exit and a RESULT: ok line.
# Ownership/Lifetime: GUID-named temporary output files are always removed.
# Links: run_probes.sh, IMPROVEMENT_AUDIT.md
# Cross-platform touchpoints: Probe names and success matching mirror run_probes.sh.
#
#===----------------------------------------------------------------------===#

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$demoDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [IO.Path]::GetFullPath((Join-Path $demoDir "..\..\.."))
$zannaSetting = [Environment]::GetEnvironmentVariable("ZANNA_BIN", "Process")
if ([string]::IsNullOrWhiteSpace($zannaSetting)) {
    $zannaSetting = "zanna"
}
if (Test-Path -LiteralPath $zannaSetting -PathType Leaf) {
    $zanna = (Resolve-Path -LiteralPath $zannaSetting).Path
} else {
    $command = Get-Command $zannaSetting -CommandType Application -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        [Console]::Error.WriteLine("Ridgebound probes: Zanna binary not found: $zannaSetting")
        [Console]::Error.WriteLine("Set ZANNA_BIN to an existing executable; this runner never builds it.")
        exit 1
    }
    $zanna = $command.Source
}

$output = Join-Path ([IO.Path]::GetTempPath()) `
    ("ridgebound-probe-{0}.txt" -f [Guid]::NewGuid().ToString("N"))
$errorOutput = "$output.stderr"

function Invoke-ZannaToCapture {
    param([Parameter(Mandatory = $true)][string[]]$NativeArguments)

    Remove-Item -LiteralPath $output, $errorOutput -Force -ErrorAction SilentlyContinue
    $startArguments = @($NativeArguments | ForEach-Object {
            $value = [string]$_
            if ($value -match '[\s"]') { '"' + $value.Replace('"', '\"') + '"' } else { $value }
        })
    $process = Start-Process -FilePath $zanna -ArgumentList $startArguments `
        -RedirectStandardOutput $output -RedirectStandardError $errorOutput `
        -NoNewWindow -Wait -PassThru
    return $process.ExitCode
}

function Read-ZannaCapture {
    $lines = @()
    if (Test-Path -LiteralPath $output -PathType Leaf) {
        $lines += @(Get-Content -LiteralPath $output)
    }
    if (Test-Path -LiteralPath $errorOutput -PathType Leaf) {
        $lines += @(Get-Content -LiteralPath $errorOutput)
    }
    return $lines
}

$passed = 0
$failed = 0
$pushed = $false
try {
    Push-Location $repoRoot
    $pushed = $true
    $checkStatus = Invoke-ZannaToCapture -NativeArguments @(
        "check", $demoDir, "--diagnostic-format=json")
    if ($checkStatus -ne 0) {
        Read-ZannaCapture | ForEach-Object { Write-Host $_ }
        [Console]::Error.WriteLine("Ridgebound probes: project check failed")
        exit 1
    }

    foreach ($probe in @("topology_probe", "traversal_probe", "state_probe", "smoke_probe")) {
        Write-Host "==> $probe"
        $probeStatus = Invoke-ZannaToCapture -NativeArguments @(
            "run", (Join-Path $demoDir "$probe.zia"))
        $lines = @(Read-ZannaCapture)
        $lines | ForEach-Object { Write-Host $_ }
        $hasResult = $null -ne ($lines | Select-String -SimpleMatch "RESULT: ok" | Select-Object -First 1)
        if ($probeStatus -eq 0 -and $hasResult) {
            ++$passed
        } else {
            [Console]::Error.WriteLine("PROBE FAILED: $probe (exit $probeStatus)")
            ++$failed
        }
    }

    Write-Host "Ridgebound probes: $passed passed, $failed failed"
    if ($failed -ne 0) {
        exit 1
    }
    exit 0
} finally {
    if ($pushed) {
        Pop-Location
    }
    Remove-Item -LiteralPath $output, $errorOutput -Force -ErrorAction SilentlyContinue
}
