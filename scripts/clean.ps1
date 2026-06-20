<#
File: scripts/clean.ps1
Purpose: Remove generated build and report directories safely.
Usage: powershell -ExecutionPolicy Bypass -File scripts/clean.ps1 [-Dirs <dir>...] [-Yes]
#>
Param(
  [string[]]$Dirs,
  [switch]$Yes
)
# clean.ps1 - remove generated directories safely.
$Root = Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..")
if (-not $Dirs -or $Dirs.Count -eq 0) {
  $Dirs = Get-ChildItem -LiteralPath $Root -Directory |
    Where-Object { $_.Name -like "build*" -or $_.Name -like "cmake-build*" -or $_.Name -eq "coverage" } |
    ForEach-Object { $_.Name }
}
if ($Dirs.Count -eq 0) { Write-Host "[clean] no generated build/report directories found at repo root"; exit 0 }
Write-Host "[clean] will remove:"; $Dirs | ForEach-Object { Write-Host "  - $_" }
if (-not $Yes) {
  $resp = Read-Host "[clean] proceed? [y/N]"
  if ($resp -notmatch '^[yY]$') { Write-Host "[clean] aborted"; exit 1 }
}
foreach ($d in $Dirs) {
  if ([string]::IsNullOrWhiteSpace($d) -or $d -eq "." -or $d -eq ".." -or $d -eq "/" -or $d -eq $Root.Path) {
    Write-Error "[clean] refusing unsafe path: $d"
    exit 2
  }
  $path = Join-Path $Root $d
  if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop; Write-Host "[clean] removed $d" }
}
Write-Host "[clean] done."
