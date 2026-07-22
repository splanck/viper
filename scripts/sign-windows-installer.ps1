#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/sign-windows-installer.ps1
# Purpose: Authenticode-sign a staged Windows installer and emit hash metadata.
# Key invariants:
#   - Failed signing or verification never replaces the requested output.
#   - Timestamp endpoints are credential-free absolute HTTPS URLs.
#   - Hash metadata is committed only after the signed artifact is published.
# Ownership/Lifetime: Same-directory temporary files are removed on every exit path.
# Links: docs/installer-release.md, scripts/build_installer.ps1
#
#===----------------------------------------------------------------------===#

param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [string]$OutputPath,
    [string]$PfxPath = $env:ZANNA_WINDOWS_SIGN_PFX,
    [string]$PfxPassword = $env:ZANNA_WINDOWS_SIGN_PASSWORD,
    [string]$Thumbprint = $env:ZANNA_WINDOWS_SIGN_THUMBPRINT,
    [string]$TimestampUrl = $(if ($env:ZANNA_WINDOWS_TIMESTAMP_URL) { $env:ZANNA_WINDOWS_TIMESTAMP_URL } else { "https://timestamp.digicert.com" }),
    [string]$SignToolPath = "signtool.exe",
    [switch]$AllowPasswordArgv,
    [switch]$NoVerify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path -Path (Get-Location) -ChildPath $Path))
}

if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf)) {
    throw "Input installer not found: $InputPath"
}
if ([string]::IsNullOrWhiteSpace($PfxPath) -and [string]::IsNullOrWhiteSpace($Thumbprint)) {
    throw "A PFX path or certificate thumbprint is required. Pass -PfxPath, -Thumbprint, or set ZANNA_WINDOWS_SIGN_PFX/ZANNA_WINDOWS_SIGN_THUMBPRINT."
}
$useThumbprint = -not [string]::IsNullOrWhiteSpace($Thumbprint)
$normalizedThumbprint = $null
if ($useThumbprint) {
    $normalizedThumbprint = ($Thumbprint -replace '\s+', '').ToUpperInvariant()
    if ($normalizedThumbprint -notmatch '^[0-9A-F]{40}$') {
        throw "Thumbprint must contain exactly 40 hexadecimal SHA-1 characters."
    }
} else {
    if (-not (Test-Path -LiteralPath $PfxPath -PathType Leaf)) {
        throw "PFX file not found: $PfxPath"
    }
    if ($null -eq $PfxPassword) {
        throw "PFX password is required. Pass -PfxPassword or set ZANNA_WINDOWS_SIGN_PASSWORD."
    }
    $passwordArgvAcknowledged = $AllowPasswordArgv -or $env:ZANNA_WINDOWS_SIGN_PASSWORD_ARGV_OK -in @("1", "true", "TRUE")
    if (-not $passwordArgvAcknowledged) {
        throw "PFX password signing exposes the password in signtool process arguments. Prefer -Thumbprint certificate-store signing, or pass -AllowPasswordArgv / set ZANNA_WINDOWS_SIGN_PASSWORD_ARGV_OK=1 to acknowledge the exposure."
    }
}

$timestampUri = $null
if (-not [System.Uri]::TryCreate($TimestampUrl, [System.UriKind]::Absolute, [ref]$timestampUri) -or
    -not [string]::Equals($timestampUri.Scheme, "https", [System.StringComparison]::OrdinalIgnoreCase) -or
    [string]::IsNullOrWhiteSpace($timestampUri.Host) -or
    -not [string]::IsNullOrEmpty($timestampUri.UserInfo) -or
    -not [string]::IsNullOrEmpty($timestampUri.Fragment)) {
    throw "TimestampUrl must be a credential-free absolute HTTPS URL without a fragment: $TimestampUrl"
}

$inputFull = Resolve-FullPath $InputPath
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = $inputFull
}
$outputFull = Resolve-FullPath $OutputPath

$unsignedHash = (Get-FileHash -LiteralPath $inputFull -Algorithm SHA256).Hash.ToLowerInvariant()

$parent = Split-Path -Parent $outputFull
if ([string]::IsNullOrWhiteSpace($parent)) {
    $parent = (Get-Location).Path
} else {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
$temporaryArtifact = Join-Path $parent `
    (".{0}.signing-{1}-{2}.tmp" -f [IO.Path]::GetFileName($outputFull), $PID, [Guid]::NewGuid().ToString("N"))
$metadataPath = "$outputFull.sha256.txt"
$temporaryMetadata = Join-Path $parent `
    (".{0}.metadata-{1}-{2}.tmp" -f [IO.Path]::GetFileName($outputFull), $PID, [Guid]::NewGuid().ToString("N"))

try {
    Copy-Item -LiteralPath $inputFull -Destination $temporaryArtifact

    $signArgs = @("sign", "/fd", "SHA256", "/tr", $TimestampUrl, "/td", "SHA256")
    if ($useThumbprint) {
        $signArgs += @("/sha1", $normalizedThumbprint)
    } else {
        $pfxFull = Resolve-FullPath $PfxPath
        $signArgs += @("/f", $pfxFull, "/p", $PfxPassword)
    }
    $signArgs += $temporaryArtifact

    & $SignToolPath @signArgs
    if ($LASTEXITCODE -ne 0) {
        throw "signtool sign failed with exit code $LASTEXITCODE"
    }

    if (-not $NoVerify) {
        & $SignToolPath verify /pa /all /tw /v $temporaryArtifact
        if ($LASTEXITCODE -ne 0) {
            throw "signtool verify failed with exit code $LASTEXITCODE"
        }
    }

    $signedHash = (Get-FileHash -LiteralPath $temporaryArtifact -Algorithm SHA256).Hash.ToLowerInvariant()
    Move-Item -LiteralPath $temporaryArtifact -Destination $outputFull -Force

    $metadataLines = @(
        "unsigned_sha256 $unsignedHash"
        "signed_sha256 $signedHash"
        "timestamp_url $TimestampUrl"
        "artifact $([System.IO.Path]::GetFileName($outputFull))"
    )
    [IO.File]::WriteAllLines($temporaryMetadata, $metadataLines, [Text.Encoding]::ASCII)
    Move-Item -LiteralPath $temporaryMetadata -Destination $metadataPath -Force
} finally {
    Remove-Item -LiteralPath $temporaryArtifact, $temporaryMetadata -Force -ErrorAction SilentlyContinue
}

Write-Output $outputFull
Write-Output $metadataPath
