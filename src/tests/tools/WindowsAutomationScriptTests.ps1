#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/tools/WindowsAutomationScriptTests.ps1
# Purpose: Verify failure-atomic signing and Windows installer/demo-driver safety contracts.
# Key invariants:
#   - A failed signer cannot replace an existing artifact or its metadata.
#   - Successful signing publishes both the artifact and canonical hash metadata.
#   - Demo automation retains single-config lookup and path-confinement guards.
#   - The cmd.exe demo compatibility entry point remains a logic-free forwarding shim.
#   - Installer automation recognizes every existing-input spelling and requires Studio by default.
# Ownership/Lifetime: The caller-owned work directory contains all temporary fixtures.
# Links: scripts/sign-windows-installer.ps1, scripts/build_demos_win.ps1,
#        scripts/build_installer.ps1
#
#===----------------------------------------------------------------------===#

param(
    [Parameter(Mandatory = $true)][string]$SignScript,
    [Parameter(Mandatory = $true)][string]$DemoScript,
    [Parameter(Mandatory = $true)][string]$InstallerScript,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-PowerShellScript {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string[]]$Arguments = @()
    )
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $Path @Arguments `
            2>$null | Out-Null
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
}

$root = [IO.Path]::GetFullPath($WorkDir)
if (Test-Path -LiteralPath $root) {
    Remove-Item -LiteralPath $root -Recurse -Force
}
[void](New-Item -ItemType Directory -Path $root)

$input = Join-Path $root "unsigned.exe"
$output = Join-Path $root "signed.exe"
$metadata = "$output.sha256.txt"
$failingSigner = Join-Path $root "failing-signtool.cmd"
$successfulSigner = Join-Path $root "successful-signtool.cmd"
[IO.File]::WriteAllBytes($input, [byte[]](0x4D, 0x5A, 0x01, 0x02, 0x03))
[IO.File]::WriteAllText($output, "existing-artifact", [Text.Encoding]::ASCII)
[IO.File]::WriteAllText($metadata, "existing-metadata", [Text.Encoding]::ASCII)
[IO.File]::WriteAllText($failingSigner, "@echo off`r`nexit /b 23`r`n", [Text.Encoding]::ASCII)
[IO.File]::WriteAllText($successfulSigner, "@echo off`r`nexit /b 0`r`n", [Text.Encoding]::ASCII)

$common = @(
    "-InputPath", $input,
    "-OutputPath", $output,
    "-Thumbprint", ("A" * 40),
    "-TimestampUrl", "https://timestamp.example.test",
    "-SignToolPath"
)
$status = Invoke-PowerShellScript -Path $SignScript -Arguments ($common + $failingSigner)
Assert-True ($status -ne 0) "A failing signtool unexpectedly succeeded."
Assert-True ([IO.File]::ReadAllText($output) -eq "existing-artifact") `
    "Failed signing replaced the existing artifact."
Assert-True ([IO.File]::ReadAllText($metadata) -eq "existing-metadata") `
    "Failed signing replaced the existing metadata."
Assert-True (@(Get-ChildItem -LiteralPath $root -Filter ".*.tmp").Count -eq 0) `
    "Failed signing left a staging file behind."

$status = Invoke-PowerShellScript -Path $SignScript -Arguments ($common + $successfulSigner)
Assert-True ($status -eq 0) "The successful signing fixture failed."
Assert-True ((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash -eq
             (Get-FileHash -LiteralPath $input -Algorithm SHA256).Hash) `
    "The staged signing artifact was not published."
$metadataText = [IO.File]::ReadAllText($metadata)
Assert-True ($metadataText.Contains("unsigned_sha256 ") -and
             $metadataText.Contains("signed_sha256 ") -and
             $metadataText.Contains("timestamp_url https://timestamp.example.test")) `
    "Signed-artifact metadata is incomplete."

$status = Invoke-PowerShellScript -Path $SignScript -Arguments @(
    "-InputPath", $input,
    "-OutputPath", $output,
    "-Thumbprint", ("A" * 40),
    "-TimestampUrl", "https://user:secret@timestamp.example.test",
    "-SignToolPath", $successfulSigner
)
Assert-True ($status -ne 0) "A credential-bearing timestamp URL was accepted."

$status = Invoke-PowerShellScript -Path $DemoScript -Arguments @("--help")
Assert-True ($status -eq 0) "The Windows demo driver help path failed."
$demoSource = [IO.File]::ReadAllText($DemoScript)
Assert-True ($demoSource.Contains('src\tools\zanna\zanna.exe')) `
    "The demo driver lacks single-config executable discovery."
Assert-True ($demoSource.Contains('Assert-CMakeTreeArchitecture')) `
    "The demo driver lacks CMake architecture validation."
Assert-True ($demoSource.Contains('Test-PathWithin')) `
    "The demo driver lacks asset and project path confinement."
Assert-True ($demoSource.Contains('duplicate demo executable name')) `
    "The demo driver lacks duplicate-output rejection."

$demoCmd = [IO.Path]::ChangeExtension($DemoScript, ".cmd")
Assert-True (Test-Path -LiteralPath $demoCmd -PathType Leaf) `
    "The cmd.exe demo compatibility entry point is missing."
$savedPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = "Continue"
    & $env:ComSpec /d /c "`"$demoCmd`" --help" 2>$null | Out-Null
    $status = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $savedPreference
}
Assert-True ($status -eq 0) "The cmd.exe demo compatibility help path failed."
$cmdSource = [IO.File]::ReadAllText($demoCmd)
Assert-True ($cmdSource.Contains("build_demos_win.ps1") -and
             $cmdSource.Contains("%*") -and
             $cmdSource.Contains("%ERRORLEVEL%")) `
    "The cmd.exe demo shim does not forward arguments and status."
Assert-True (-not $cmdSource.Contains("cmake") -and -not $cmdSource.Contains("zanna build")) `
    "The cmd.exe demo shim duplicates build logic."

$status = Invoke-PowerShellScript -Path $InstallerScript -Arguments @("--help")
Assert-True ($status -eq 0) "The Windows installer wrapper help path failed."
$installerSource = [IO.File]::ReadAllText($InstallerScript)
Assert-True ($installerSource.Contains('StartsWith("--build-dir="') -and
             $installerSource.Contains('StartsWith("--stage-dir="') -and
             $installerSource.Contains('StartsWith("--verify-only="')) `
    "The installer wrapper does not recognize equals-form existing inputs."
Assert-True ($installerSource.Contains("ZANNA_INSTALL_ZANNASTUDIO") -and
             $installerSource.Contains("zannastudio.buildinfo")) `
    "The installer wrapper does not verify the default Zanna Studio build."
Assert-True ($installerSource.Contains("[IO.Path]::GetFullPath(`$buildDir)")) `
    "The installer wrapper does not normalize an absolute build directory."

Write-Host "Windows automation script tests passed."
