#===----------------------------------------------------------------------===#
#
# Part of the Viper project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/tools/WindowsUpdateManifestAuthoringTests.ps1
# Purpose: Exercise deterministic signed Windows update-manifest authoring.
#
# Key invariants:
#   - The ephemeral test certificate is removed from the user store in finally.
#   - Generated manifests are canonical UTF-8/LF and deterministic.
#   - Unsafe origins and ambiguous SemVer spellings fail before publication.
#
# Ownership/Lifetime: Test artifacts are isolated under the supplied build directory.
#
# Links: scripts/new-windows-update-manifest.ps1, WindowsInstallerUpdate.cpp
#
#===----------------------------------------------------------------------===#

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ManifestScript,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Fails {
    param([scriptblock]$Action, [string]$Pattern)
    try {
        & $Action
    } catch {
        if ($_.Exception.Message -match $Pattern) {
            return
        }
        throw "Expected failure matching '$Pattern', got: $($_.Exception.Message)"
    }
    throw "Expected command to fail with '$Pattern'."
}

$scriptPath = [IO.Path]::GetFullPath($ManifestScript)
$workPath = [IO.Path]::GetFullPath($WorkDir)
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "Manifest authoring script is missing: $scriptPath"
}
if ($workPath -eq [IO.Path]::GetPathRoot($workPath) -or $workPath.Length -lt 10) {
    throw "Refusing unsafe update-manifest test directory: $workPath"
}
Remove-Item -LiteralPath $workPath -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $workPath -Force | Out-Null

$certificate = $null
try {
    $certificate = New-SelfSignedCertificate `
        -Subject "CN=Viper Update Manifest Test $PID" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -KeyExportPolicy Exportable `
        -HashAlgorithm SHA256 `
        -NotAfter ([DateTime]::UtcNow.AddDays(1))
    $passwordText = "viper-update-test-$PID"
    $password = ConvertTo-SecureString $passwordText -AsPlainText -Force
    $pfx = Join-Path $workPath "update-signing-test.pfx"
    Export-PfxCertificate `
        -Cert $certificate `
        -FilePath $pfx `
        -Password $password `
        -ChainOption EndEntityCertOnly | Out-Null

    $common = @{
        ManifestUrl = "https://updates.example.test/viper/windows-x64.txt"
        Channel = "stable"
        Architecture = "x64"
        Version = "1.2.3-rc.1+build.9"
        DownloadUrl = "https://updates.example.test/viper/viper-1.2.3-x64.exe"
        DownloadSha256 = ("ab" * 32)
        ReleaseNotesUrl = "https://updates.example.test/viper/1.2.3.html"
        PfxPath = $pfx
        PfxPassword = $passwordText
    }
    $first = Join-Path $workPath "update-first.txt"
    $second = Join-Path $workPath "update-second.txt"
    $public = Join-Path $workPath "update-public-key.json"
    $publicOnly = Join-Path $workPath "update-public-key-only.json"
    & $scriptPath -PublicKeyOnly -PublicKeyOutput $publicOnly `
        -PfxPath $pfx -PfxPassword $passwordText | Out-Null
    & $scriptPath @common -OutputPath $first -PublicKeyOutput $public | Out-Null
    & $scriptPath @common -OutputPath $second | Out-Null

    $firstBytes = [IO.File]::ReadAllBytes($first)
    $secondBytes = [IO.File]::ReadAllBytes($second)
    Assert-True ($firstBytes.Length -gt 0) "Generated update manifest is empty."
    Assert-True (-not ($firstBytes.Length -ge 3 -and $firstBytes[0] -eq 0xef -and
            $firstBytes[1] -eq 0xbb -and $firstBytes[2] -eq 0xbf)) `
        "Generated update manifest unexpectedly has a UTF-8 BOM."
    Assert-True (-not ($firstBytes -contains 13)) "Generated update manifest contains CR bytes."
    $firstHash = (Get-FileHash -LiteralPath $first -Algorithm SHA256).Hash
    $secondHash = (Get-FileHash -LiteralPath $second -Algorithm SHA256).Hash
    Assert-True ($firstHash -eq $secondHash) `
        "Identical update inputs did not produce identical signed manifests."

    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $text = $utf8.GetString($firstBytes)
    $lines = $text.Split("`n")
    Assert-True ($lines.Count -eq 9 -and $lines[8] -eq "") `
        "Update manifest does not contain exactly eight LF-terminated records."
    Assert-True ($lines[0] -eq "VIPER-WINDOWS-UPDATE`t1") "Wrong update manifest header."
    Assert-True ($lines[1] -eq "channel`tstable") "Wrong update manifest channel."
    Assert-True ($lines[7] -match '^signature\t[0-9a-f]{512}$') `
        "Update signature is missing or has the wrong RSA-2048 encoding."

    $publicKey = Get-Content -LiteralPath $public -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-True ($publicKey.schema -eq 1) "Wrong public-key schema."
    Assert-True ($publicKey.algorithm -eq "RSA-PKCS1-SHA256") "Wrong public-key algorithm."
    Assert-True ($publicKey.modulus -match '^[0-9a-f]{512}$') "Wrong RSA modulus encoding."
    Assert-True ($publicKey.exponent -eq "010001") "Unexpected RSA public exponent."
    $publicOnlyKey = Get-Content -LiteralPath $publicOnly -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-True ($publicOnlyKey.modulus -eq $publicKey.modulus) `
        "Public-key-only export did not use the signing certificate's modulus."
    Assert-True ($publicOnlyKey.exponent -eq $publicKey.exponent) `
        "Public-key-only export did not use the signing certificate's exponent."

    $badCore = $common.Clone()
    $badCore["Version"] = "01.2.3"
    Assert-Fails {
        & $scriptPath @badCore -OutputPath (Join-Path $workPath "bad-core.txt")
    } "non-canonical"
    $badPrerelease = $common.Clone()
    $badPrerelease["Version"] = "1.2.3-rc.01"
    Assert-Fails {
        & $scriptPath @badPrerelease -OutputPath (Join-Path $workPath "bad-prerelease.txt")
    } "leading zero"
    $badOverflow = $common.Clone()
    $badOverflow["Version"] = "1.2.2147483648"
    Assert-Fails {
        & $scriptPath @badOverflow -OutputPath (Join-Path $workPath "bad-overflow.txt")
    } "overflowing"
    $badOrigin = $common.Clone()
    $badOrigin["DownloadUrl"] = "https://downloads.example.test/viper.exe"
    Assert-Fails {
        & $scriptPath @badOrigin -OutputPath (Join-Path $workPath "bad-origin.txt")
    } "share the update-manifest service origin"
    $badHttp = $common.Clone()
    $badHttp["ManifestUrl"] = "http://updates.example.test/viper/windows-x64.txt"
    Assert-Fails {
        & $scriptPath @badHttp -OutputPath (Join-Path $workPath "bad-http.txt")
    } "absolute HTTPS URL"
} finally {
    if ($null -ne $certificate) {
        Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" `
            -Force -ErrorAction SilentlyContinue
        $certificate.Dispose()
    }
}
