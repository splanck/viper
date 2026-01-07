<#
File: scripts/clean.ps1
Purpose: Remove CMake build directories safely.
Usage: powershell -ExecutionPolicy Bypass -File scripts/clean.ps1 [-Dirs <dir>...] [-Yes]
#>
Param(
  [string[]]$Dirs,
  [switch]$Yes
)
# clean.ps1 — remove CMake build directories safely.
$Root = Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..")
if (-not $Dirs -or $Dirs.Count -eq 0) {
  $Dirs = Get-ChildItem -LiteralPath $Root -Directory -Filter "build*" | ForEach-Object { $_.Name }
}
if ($Dirs.Count -eq 0) { Write-Host "[clean] no build* directories found at repo root"; exit 0 }
Write-Host "[clean] will remove:"; $Dirs | ForEach-Object { Write-Host "  - $_" }
if (-not $Yes) {
  $resp = Read-Host "[clean] proceed? [y/N]"
  if ($resp -notmatch '^[yY]$') { Write-Host "[clean] aborted"; exit 1 }
}
foreach ($d in $Dirs) {
  $path = Join-Path $Root $d
  if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop; Write-Host "[clean] removed $d" }
}
Write-Host "[clean] done."
