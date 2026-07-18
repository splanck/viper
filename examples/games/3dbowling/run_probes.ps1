#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: examples/games/3dbowling/run_probes.ps1
# Purpose: Run every 3dbowling release gate with an existing Zanna binary.
# Key invariants:
#   - The script never configures, builds, or invokes CTest.
#   - A probe passes only when it exits cleanly and prints exactly RESULT: ok.
# Ownership/Lifetime: GUID-named temporary output files are always removed.
# Links: run_probes.sh, LONG_TERM_IMPROVEMENT_SPEC.md
# Cross-platform touchpoints: Probe names and exact success matching mirror run_probes.sh.
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
        [Console]::Error.WriteLine("3dbowling probes: Zanna binary not found: $zannaSetting")
        [Console]::Error.WriteLine("Set ZANNA_BIN to an existing zanna executable; this runner never builds it.")
        exit 1
    }
    $zanna = $command.Source
}

$output = Join-Path ([IO.Path]::GetTempPath()) `
    ("3dbowling-probe-{0}.txt" -f [Guid]::NewGuid().ToString("N"))
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

$probes = @(
    "release_upgrade_probe", "pinfall_contract_probe", "impact_order_probe",
    "oil_grid_probe", "trajectory_probe", "ai_delivery_probe", "feedback_probe",
    "stability_probe", "replay_scene_probe", "lifecycle_probe", "asset_resolution_probe",
    "save_clamp_probe", "frame_rate_probe", "layout_probe", "accessibility_probe",
    "menu_flow_probe", "match_mode_probe", "asset_probe", "asset_render_probe",
    "aim_smoke_probe", "overlay_smoke_probe", "smoke_probe", "title_nopostfx_smoke",
    "title_postfx_smoke", "scene_nopostfx_smoke", "release_visual_probe",
    "release_menu_probe"
)
$passed = 0
$failed = 0
$pushed = $false
try {
    Push-Location $repoRoot
    $pushed = $true
    foreach ($probe in $probes) {
        Write-Host "==> $probe"
        $probeStatus = Invoke-ZannaToCapture -NativeArguments @(
            "run", (Join-Path $demoDir "$probe.zia"))
        $lines = @(Read-ZannaCapture)
        $lines | ForEach-Object { Write-Host $_ }
        $hasExactResult = $null -ne ($lines | Where-Object { $_ -ceq "RESULT: ok" } |
            Select-Object -First 1)
        if ($probeStatus -eq 0 -and $hasExactResult) {
            ++$passed
        } else {
            [Console]::Error.WriteLine("PROBE FAILED: $probe (exit $probeStatus)")
            ++$failed
        }
    }

    Write-Host "3dbowling probes: $passed passed, $failed failed"
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
