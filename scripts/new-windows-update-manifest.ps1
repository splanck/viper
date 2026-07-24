#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/new-windows-update-manifest.ps1
# Purpose: Create and verify the canonical signed Windows installer update manifest.
#
# Key invariants:
#   - The signing password is read from the environment by default and is never logged.
#   - Release and notes URLs are HTTPS and share the manifest service origin.
#   - Canonical UTF-8/LF bytes exactly match the native installer verifier.
#
# Ownership/Lifetime: Private-key objects are disposed before the script exits.
#
# Links: WindowsInstallerUpdate.cpp, docs/installer-release.md
#
#===----------------------------------------------------------------------===#

[CmdletBinding(DefaultParameterSetName = "Pfx")]
param(
    [string]$OutputPath = "",

    [string]$ManifestUrl = "",

    [ValidatePattern('^[a-z0-9](?:[a-z0-9-]{0,22}[a-z0-9])?$')]
    [string]$Channel = "",

    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "",

    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$')]
    [string]$Version = "",

    [string]$DownloadUrl = "",

    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$DownloadSha256 = "",

    [string]$ReleaseNotesUrl = "",

    [Parameter(Mandatory = $true, ParameterSetName = "Pfx")]
    [string]$PfxPath,

    [Parameter(ParameterSetName = "Pfx")]
    [string]$PfxPassword = $env:ZANNA_WINDOWS_UPDATE_SIGN_PASSWORD,

    [Parameter(Mandatory = $true, ParameterSetName = "CertificateStore")]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$CertificateThumbprint,

    [ValidateSet("CurrentUser", "LocalMachine")]
    [string]$CertificateStoreLocation = "CurrentUser",

    [string]$PublicKeyOutput,

    [switch]$PublicKeyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
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

function Assert-SafeFileOutput {
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
    $current = $Path
    $isLeaf = $true
    while (-not [string]::IsNullOrWhiteSpace($current)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Description traverses a reparse point: $current"
            }
            $linkTypeProperty = $item.PSObject.Properties["LinkType"]
            if ($isLeaf -and $linkTypeProperty -and
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
        $isLeaf = $false
    }
}

function Publish-AtomicFileSet {
    param([Parameter(Mandatory = $true)][object[]]$Entries)

    $states = [Collections.Generic.List[object]]::new()
    foreach ($entry in $Entries) {
        $destination = [string]$entry.Destination
        $parent = Split-Path -Parent $destination
        [void](New-Item -ItemType Directory -Path $parent -Force)
        Assert-SafeFileOutput -Path $destination -Description "Update metadata output"
        $backup = Join-Path $parent (
            ".zupdate-backup-$PID-$([Guid]::NewGuid().ToString('N')).bak")
        $states.Add([pscustomobject]@{
            Source = [string]$entry.Source
            Destination = $destination
            Backup = $backup
            HadDestination = Test-Path -LiteralPath $destination -PathType Leaf
            BackedUp = $false
            Published = $false
        })
    }
    $success = $false
    try {
        foreach ($state in $states) {
            if ($state.HadDestination) {
                Move-Item -LiteralPath $state.Destination -Destination $state.Backup
                $state.BackedUp = $true
            }
        }
        foreach ($state in $states) {
            Move-Item -LiteralPath $state.Source -Destination $state.Destination
            $state.Published = $true
        }
        $success = $true
    } catch {
        for ($stateIndex = $states.Count - 1; $stateIndex -ge 0; --$stateIndex) {
            $state = $states[$stateIndex]
            if ($state.Published -and
                (Test-Path -LiteralPath $state.Destination -PathType Leaf)) {
                Remove-Item -LiteralPath $state.Destination -Force
            }
        }
        for ($stateIndex = $states.Count - 1; $stateIndex -ge 0; --$stateIndex) {
            $state = $states[$stateIndex]
            if ($state.BackedUp -and (Test-Path -LiteralPath $state.Backup -PathType Leaf)) {
                Move-Item -LiteralPath $state.Backup -Destination $state.Destination
            }
        }
        throw
    } finally {
        foreach ($state in $states) {
            if (Test-Path -LiteralPath $state.Source -PathType Leaf) {
                Remove-Item -LiteralPath $state.Source -Force
            }
            if ($success -and (Test-Path -LiteralPath $state.Backup -PathType Leaf)) {
                Remove-Item -LiteralPath $state.Backup -Force
            }
        }
    }
}

function Convert-ToLowerHex {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    return -join ($Bytes | ForEach-Object { $_.ToString("x2", [Globalization.CultureInfo]::InvariantCulture) })
}

function Get-HttpsUri {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Field
    )

    $uri = $null
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value.Length -gt 2048 -or
        $Value -match '[^\x21-\x7e]' -or $Value.Contains('\') -or
        -not [Uri]::TryCreate($Value, [UriKind]::Absolute, [ref]$uri) -or
        -not [string]::Equals($uri.Scheme, "https", [StringComparison]::OrdinalIgnoreCase) -or
        [string]::IsNullOrWhiteSpace($uri.Host) -or
        -not [string]::IsNullOrEmpty($uri.UserInfo) -or
        -not [string]::IsNullOrEmpty($uri.Fragment)) {
        throw "$Field must be an absolute HTTPS URL without credentials or a fragment."
    }
    return $uri
}

function Assert-SameOrigin {
    param(
        [Parameter(Mandatory = $true)][Uri]$Expected,
        [Parameter(Mandatory = $true)][Uri]$Actual,
        [Parameter(Mandatory = $true)][string]$Field
    )

    if (-not [string]::Equals($Expected.Scheme, $Actual.Scheme, [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals($Expected.DnsSafeHost, $Actual.DnsSafeHost, [StringComparison]::OrdinalIgnoreCase) -or
        $Expected.Port -ne $Actual.Port) {
        throw "$Field must share the update-manifest service origin."
    }
}

function Assert-CanonicalVersion {
    param([Parameter(Mandatory = $true)][string]$Value)

    $buildSeparator = $Value.IndexOf('+')
    $withoutBuild = if ($buildSeparator -ge 0) {
        $Value.Substring(0, $buildSeparator)
    } else {
        $Value
    }
    $prereleaseSeparator = $withoutBuild.IndexOf('-')
    $coreText = if ($prereleaseSeparator -ge 0) {
        $withoutBuild.Substring(0, $prereleaseSeparator)
    } else {
        $withoutBuild
    }
    $core = $coreText.Split('.')
    if ($core.Count -lt 3 -or $core.Count -gt 4) {
        throw "Version must contain three or four numeric core components."
    }
    foreach ($component in $core) {
        $parsed = [Int32]0
        if ($component.Length -gt 9 -or
            ($component.Length -gt 1 -and $component[0] -eq '0') -or
            -not [Int32]::TryParse(
                $component,
                [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture,
                [ref]$parsed)) {
            throw "Version has a non-canonical or overflowing numeric component: $component"
        }
    }
    if ($prereleaseSeparator -ge 0) {
        $prerelease = $withoutBuild.Substring($prereleaseSeparator + 1)
        foreach ($identifier in $prerelease.Split('.')) {
            $numeric = $identifier -match '^[0-9]+$'
            if ($numeric -and $identifier.Length -gt 1 -and $identifier[0] -eq '0') {
                throw "Version has a numeric prerelease identifier with a leading zero: $identifier"
            }
        }
    }
}

function Assert-RequiredText {
    param(
        [AllowNull()][string]$Value,
        [Parameter(Mandatory = $true)][string]$Field
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "$Field is required unless -PublicKeyOnly is used."
    }
}

$manifestPath = $null
$publicKeyPath = $null
if ($PublicKeyOnly) {
    if ([string]::IsNullOrWhiteSpace($PublicKeyOutput)) {
        throw "-PublicKeyOnly requires -PublicKeyOutput."
    }
} else {
    Assert-RequiredText $OutputPath "OutputPath"
    Assert-RequiredText $ManifestUrl "ManifestUrl"
    Assert-RequiredText $Channel "Channel"
    Assert-RequiredText $Architecture "Architecture"
    Assert-RequiredText $Version "Version"
    Assert-RequiredText $DownloadUrl "DownloadUrl"
    Assert-RequiredText $DownloadSha256 "DownloadSha256"
    $manifestPath = Resolve-FullPath $OutputPath
    Assert-CanonicalVersion $Version
    $manifestUri = Get-HttpsUri $ManifestUrl "ManifestUrl"
    $downloadUri = Get-HttpsUri $DownloadUrl "DownloadUrl"
    Assert-SameOrigin $manifestUri $downloadUri "DownloadUrl"
    if (-not [string]::IsNullOrWhiteSpace($ReleaseNotesUrl)) {
        $notesUri = Get-HttpsUri $ReleaseNotesUrl "ReleaseNotesUrl"
        Assert-SameOrigin $manifestUri $notesUri "ReleaseNotesUrl"
    }
}
if (-not [string]::IsNullOrWhiteSpace($PublicKeyOutput)) {
    $publicKeyPath = Resolve-FullPath $PublicKeyOutput
}
if ($manifestPath -and $publicKeyPath -and
    (Test-PathsEqual -Left $manifestPath -Right $publicKeyPath)) {
    throw "OutputPath and PublicKeyOutput must be distinct files."
}
if ($manifestPath) {
    Assert-SafeFileOutput -Path $manifestPath -Description "Update manifest output"
}
if ($publicKeyPath) {
    Assert-SafeFileOutput -Path $publicKeyPath -Description "Update public-key output"
}

$certificate = $null
$rsa = $null
$stagedOutputs = [Collections.Generic.List[string]]::new()
try {
    if ($PSCmdlet.ParameterSetName -eq "Pfx") {
        $pfxFull = Resolve-FullPath $PfxPath
        if (-not (Test-Path -LiteralPath $pfxFull -PathType Leaf)) {
            throw "Update-signing PFX not found: $pfxFull"
        }
        Assert-SafeFileOutput -Path $pfxFull -Description "Update-signing PFX"
        if (($manifestPath -and (Test-PathsEqual -Left $pfxFull -Right $manifestPath)) -or
            ($publicKeyPath -and (Test-PathsEqual -Left $pfxFull -Right $publicKeyPath))) {
            throw "The update-signing PFX must not alias a generated output."
        }
        if ($null -eq $PfxPassword) {
            throw "Set ZANNA_WINDOWS_UPDATE_SIGN_PASSWORD or pass -PfxPassword."
        }
        $flags = [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet
        $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
            $pfxFull, $PfxPassword, $flags)
    } else {
        $normalized = $CertificateThumbprint.ToUpperInvariant()
        $storeLocation = [Security.Cryptography.X509Certificates.StoreLocation]::$CertificateStoreLocation
        $store = [Security.Cryptography.X509Certificates.X509Store]::new("My", $storeLocation)
        $matches = $null
        try {
            $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
            $matches = $store.Certificates.Find(
                [Security.Cryptography.X509Certificates.X509FindType]::FindByThumbprint,
                $normalized,
                $false)
            if ($matches.Count -ne 1) {
                throw "Expected exactly one update-signing certificate with thumbprint $normalized."
            }
            $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($matches[0])
        } finally {
            if ($null -ne $matches) {
                foreach ($match in $matches) {
                    $match.Dispose()
                }
            }
            $store.Dispose()
        }
    }

    $rsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey(
        $certificate)
    if ($null -eq $rsa) {
        throw "The update-signing certificate has no accessible RSA private key."
    }
    if ($rsa.KeySize -lt 2048 -or $rsa.KeySize -gt 4096) {
        throw "The update-signing RSA key must be between 2048 and 4096 bits."
    }
    $public = $rsa.ExportParameters($false)
    $modulus = Convert-ToLowerHex $public.Modulus
    $exponent = Convert-ToLowerHex $public.Exponent
    $utf8 = [Text.UTF8Encoding]::new($false, $true)

    $publicTemporary = $null
    if ($publicKeyPath) {
        $publicParent = Split-Path -Parent $publicKeyPath
        [void](New-Item -ItemType Directory -Path $publicParent -Force)
        Assert-SafeFileOutput -Path $publicKeyPath -Description "Update public-key output"
        $publicTemporary = Join-Path $publicParent (
            ".zupdate-public-$PID-$([Guid]::NewGuid().ToString('N')).tmp")
        $stagedOutputs.Add($publicTemporary)
        $publicJson = [ordered]@{
            schema = 1
            algorithm = "RSA-PKCS1-SHA256"
            modulus = $modulus
            exponent = $exponent
        } | ConvertTo-Json
        [IO.File]::WriteAllText($publicTemporary, $publicJson + "`n", $utf8)
    }
    if ($PublicKeyOnly) {
        Publish-AtomicFileSet -Entries @(
            [pscustomobject]@{ Source = $publicTemporary; Destination = $publicKeyPath })
        return [pscustomobject]@{
            Manifest = $null
            ManifestSha256 = $null
            PublicKey = $publicKeyPath
            RsaModulus = $modulus
            RsaExponent = $exponent
        }
    }

    $lines = @(
        "ZANNA-WINDOWS-UPDATE`t1",
        "channel`t$Channel",
        "architecture`t$Architecture",
        "version`t$Version",
        "download-url`t$DownloadUrl",
        "sha256`t$($DownloadSha256.ToLowerInvariant())",
        "release-notes-url`t$ReleaseNotesUrl"
    )
    foreach ($line in $lines) {
        if ($line.IndexOf("`r") -ge 0 -or $line.IndexOf("`n") -ge 0 -or
            $utf8.GetByteCount($line) -gt 8192) {
            throw "An update manifest field contains an invalid line break or is too long."
        }
    }
    $canonical = ($lines -join "`n") + "`n"
    $canonicalBytes = $utf8.GetBytes($canonical)
    $signature = $rsa.SignData(
        $canonicalBytes,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    if (-not $rsa.VerifyData(
            $canonicalBytes,
            $signature,
            [Security.Cryptography.HashAlgorithmName]::SHA256,
            [Security.Cryptography.RSASignaturePadding]::Pkcs1)) {
        throw "The generated update signature did not verify."
    }
    if ($signature.Length -ne [int]($rsa.KeySize / 8)) {
        throw "The generated update signature has an unexpected RSA length."
    }
    $manifest = $canonical + "signature`t$(Convert-ToLowerHex $signature)`n"
    if ($utf8.GetByteCount($manifest) -gt 64KB) {
        throw "The update manifest exceeds the installer's 64 KiB limit."
    }

    $parent = Split-Path -Parent $manifestPath
    [void](New-Item -ItemType Directory -Path $parent -Force)
    Assert-SafeFileOutput -Path $manifestPath -Description "Update manifest output"
    $temporary = Join-Path $parent (
        ".zupdate-manifest-$PID-$([Guid]::NewGuid().ToString('N')).tmp")
    $stagedOutputs.Add($temporary)
    [IO.File]::WriteAllText($temporary, $manifest, $utf8)
    $publishEntries = [Collections.Generic.List[object]]::new()
    $publishEntries.Add(
        [pscustomobject]@{ Source = $temporary; Destination = $manifestPath })
    if ($publicKeyPath) {
        $publishEntries.Add(
            [pscustomobject]@{ Source = $publicTemporary; Destination = $publicKeyPath })
    }
    Publish-AtomicFileSet -Entries $publishEntries.ToArray()

    [pscustomobject]@{
        Manifest = $manifestPath
        ManifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
        PublicKey = $publicKeyPath
        RsaModulus = $modulus
        RsaExponent = $exponent
    }
} finally {
    foreach ($stagedOutput in $stagedOutputs) {
        Remove-Item -LiteralPath $stagedOutput -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $rsa) {
        $rsa.Dispose()
    }
    if ($null -ne $certificate) {
        $certificate.Dispose()
    }
}
