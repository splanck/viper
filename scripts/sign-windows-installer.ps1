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
#   - The signed artifact and hash metadata publish as one rollback-protected pair.
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

function Test-PathsEqual {
    param(
        [Parameter(Mandatory = $true)][string]$Left,
        [Parameter(Mandatory = $true)][string]$Right
    )
    return [string]::Equals(
        (Resolve-FullPath $Left).TrimEnd('\', '/'),
        (Resolve-FullPath $Right).TrimEnd('\', '/'),
        [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoPathIndirection {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description,
        [switch]$IncludeLeaf
    )

    $current = if ($IncludeLeaf) { Resolve-FullPath $Path } else {
        Split-Path -Parent (Resolve-FullPath $Path)
    }
    while (-not [string]::IsNullOrWhiteSpace($current)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Description traverses a reparse point: $current"
            }
            $linkTypeProperty = $item.PSObject.Properties["LinkType"]
            if ($IncludeLeaf -and $linkTypeProperty -and
                $linkTypeProperty.Value -eq "HardLink") {
                throw "$Description must not be a hard-link alias: $current"
            }
        }
        $parent = Split-Path -Parent $current
        if ([string]::IsNullOrWhiteSpace($parent) -or
            [string]::Equals($parent, $current, [StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        $current = $parent
        $IncludeLeaf = $false
    }
}

function Assert-FileDestination {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($Path.Length -ge 32760) {
        throw "$Description exceeds the supported Windows path length."
    }
    if ((Test-Path -LiteralPath $Path) -and
        -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description is not a regular file destination: $Path"
    }
    Assert-NoPathIndirection -Path $Path -Description $Description -IncludeLeaf
}

$inputFull = Resolve-FullPath $InputPath
if (-not (Test-Path -LiteralPath $inputFull -PathType Leaf)) {
    throw "Input installer not found: $inputFull"
}
Assert-NoPathIndirection -Path $inputFull -Description "Input installer" -IncludeLeaf
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
    $pfxFull = Resolve-FullPath $PfxPath
    if (-not (Test-Path -LiteralPath $pfxFull -PathType Leaf)) {
        throw "PFX file not found: $pfxFull"
    }
    Assert-NoPathIndirection -Path $pfxFull -Description "PFX file" -IncludeLeaf
    if ($null -eq $PfxPassword) {
        throw "PFX password is required. Pass -PfxPassword or set ZANNA_WINDOWS_SIGN_PASSWORD."
    }
    $passwordArgvAcknowledged = $AllowPasswordArgv -or $env:ZANNA_WINDOWS_SIGN_PASSWORD_ARGV_OK -in @("1", "true", "TRUE")
    if (-not $passwordArgvAcknowledged) {
        throw "PFX password signing exposes the password in signtool process arguments. Prefer -Thumbprint certificate-store signing, or pass -AllowPasswordArgv / set ZANNA_WINDOWS_SIGN_PASSWORD_ARGV_OK=1 to acknowledge the exposure."
    }
}

$timestampUri = $null
if ([string]::IsNullOrWhiteSpace($TimestampUrl) -or $TimestampUrl.Length -gt 2048 -or
    $TimestampUrl -match '[^\x21-\x7e]' -or $TimestampUrl.Contains('\') -or
    -not [System.Uri]::TryCreate($TimestampUrl, [System.UriKind]::Absolute, [ref]$timestampUri) -or
    -not [string]::Equals($timestampUri.Scheme, "https", [System.StringComparison]::OrdinalIgnoreCase) -or
    [string]::IsNullOrWhiteSpace($timestampUri.Host) -or
    -not [string]::IsNullOrEmpty($timestampUri.UserInfo) -or
    -not [string]::IsNullOrEmpty($timestampUri.Fragment)) {
    throw "TimestampUrl must be a credential-free absolute HTTPS URL without a fragment: $TimestampUrl"
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = $inputFull
}
$outputFull = Resolve-FullPath $OutputPath
$parent = Split-Path -Parent $outputFull
if ([string]::IsNullOrWhiteSpace($parent)) {
    $parent = (Get-Location).Path
} else {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
$metadataPath = "$outputFull.sha256.txt"
Assert-FileDestination -Path $outputFull -Description "Signed installer output"
Assert-FileDestination -Path $metadataPath -Description "Signed installer metadata"
if ((Test-PathsEqual -Left $outputFull -Right $metadataPath) -or
    (Test-PathsEqual -Left $inputFull -Right $metadataPath)) {
    throw "Artifact, input, and metadata paths must be distinct."
}
if (-not $useThumbprint) {
    if ((Test-PathsEqual -Left $pfxFull -Right $inputFull) -or
        (Test-PathsEqual -Left $pfxFull -Right $outputFull) -or
        (Test-PathsEqual -Left $pfxFull -Right $metadataPath)) {
        throw "The PFX path must not alias an input or output artifact."
    }
}

$token = "$PID-$([Guid]::NewGuid().ToString('N'))"
$temporaryArtifact = Join-Path $parent ".zsig-$token.tmp"
$temporaryMetadata = Join-Path $parent ".zmeta-$token.tmp"
$outputBackup = Join-Path $parent ".zout-$token.bak"
$metadataBackup = Join-Path $parent ".zmetabak-$token.bak"
$publicationSucceeded = $false
$hadOutput = Test-Path -LiteralPath $outputFull -PathType Leaf
$hadMetadata = Test-Path -LiteralPath $metadataPath -PathType Leaf
$publishedOutput = $false
$publishedMetadata = $false
$backedUpOutput = $false
$backedUpMetadata = $false
$unsignedHash = (Get-FileHash -LiteralPath $inputFull -Algorithm SHA256).Hash.ToLowerInvariant()

try {
    Copy-Item -LiteralPath $inputFull -Destination $temporaryArtifact
    $copiedHash = (Get-FileHash -LiteralPath $temporaryArtifact -Algorithm SHA256).Hash.ToLowerInvariant()
    $inputHashAfterCopy =
        (Get-FileHash -LiteralPath $inputFull -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($copiedHash -cne $unsignedHash -or $inputHashAfterCopy -cne $unsignedHash) {
        throw "Input installer changed while it was being staged for signing."
    }

    $signArgs = @("sign", "/fd", "SHA256", "/tr", $TimestampUrl, "/td", "SHA256")
    if ($useThumbprint) {
        $signArgs += @("/sha1", $normalizedThumbprint)
    } else {
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

    if (-not (Test-Path -LiteralPath $temporaryArtifact -PathType Leaf) -or
        (Get-Item -LiteralPath $temporaryArtifact).Length -le 0) {
        throw "signtool did not leave a non-empty regular artifact."
    }
    Assert-NoPathIndirection -Path $temporaryArtifact `
        -Description "Staged signed installer" -IncludeLeaf
    $signedHash = (Get-FileHash -LiteralPath $temporaryArtifact -Algorithm SHA256).Hash.ToLowerInvariant()
    $inputHashBeforePublish =
        (Get-FileHash -LiteralPath $inputFull -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($inputHashBeforePublish -cne $unsignedHash) {
        throw "Input installer changed before the signed artifact could be published."
    }

    $metadataLines = @(
        "unsigned_sha256 $unsignedHash"
        "signed_sha256 $signedHash"
        "timestamp_url $TimestampUrl"
        "artifact $([System.IO.Path]::GetFileName($outputFull))"
    )
    [IO.File]::WriteAllLines(
        $temporaryMetadata, $metadataLines, [Text.UTF8Encoding]::new($false))

    try {
        if ($hadOutput) {
            Move-Item -LiteralPath $outputFull -Destination $outputBackup
            $backedUpOutput = $true
        }
        if ($hadMetadata) {
            Move-Item -LiteralPath $metadataPath -Destination $metadataBackup
            $backedUpMetadata = $true
        }
        Move-Item -LiteralPath $temporaryArtifact -Destination $outputFull
        $publishedOutput = $true
        Move-Item -LiteralPath $temporaryMetadata -Destination $metadataPath
        $publishedMetadata = $true
        $publicationSucceeded = $true
    } catch {
        if ($publishedMetadata -and (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
            Remove-Item -LiteralPath $metadataPath -Force
        }
        if ($publishedOutput -and (Test-Path -LiteralPath $outputFull -PathType Leaf)) {
            Remove-Item -LiteralPath $outputFull -Force
        }
        if ($backedUpMetadata -and (Test-Path -LiteralPath $metadataBackup -PathType Leaf)) {
            Move-Item -LiteralPath $metadataBackup -Destination $metadataPath
        }
        if ($backedUpOutput -and (Test-Path -LiteralPath $outputBackup -PathType Leaf)) {
            Move-Item -LiteralPath $outputBackup -Destination $outputFull
        }
        throw
    }
} finally {
    Remove-Item -LiteralPath $temporaryArtifact, $temporaryMetadata -Force -ErrorAction SilentlyContinue
    if ($publicationSucceeded) {
        Remove-Item -LiteralPath $outputBackup, $metadataBackup -Force -ErrorAction SilentlyContinue
    }
}

Write-Output $outputFull
Write-Output $metadataPath
