param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [string]$OutputPath,
    [string]$PfxPath = $env:VIPER_WINDOWS_SIGN_PFX,
    [string]$PfxPassword = $env:VIPER_WINDOWS_SIGN_PASSWORD,
    [string]$Thumbprint = $env:VIPER_WINDOWS_SIGN_THUMBPRINT,
    [string]$TimestampUrl = $(if ($env:VIPER_WINDOWS_TIMESTAMP_URL) { $env:VIPER_WINDOWS_TIMESTAMP_URL } else { "https://timestamp.digicert.com" }),
    [string]$SignToolPath = "signtool.exe",
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
    throw "A PFX path or certificate thumbprint is required. Pass -PfxPath, -Thumbprint, or set VIPER_WINDOWS_SIGN_PFX/VIPER_WINDOWS_SIGN_THUMBPRINT."
}
if (-not [string]::IsNullOrWhiteSpace($PfxPath)) {
    if (-not (Test-Path -LiteralPath $PfxPath -PathType Leaf)) {
        throw "PFX file not found: $PfxPath"
    }
    if ($null -eq $PfxPassword) {
        throw "PFX password is required. Pass -PfxPassword or set VIPER_WINDOWS_SIGN_PASSWORD."
    }
}

$inputFull = Resolve-FullPath $InputPath
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = $inputFull
}
$outputFull = Resolve-FullPath $OutputPath

$unsignedHash = (Get-FileHash -LiteralPath $inputFull -Algorithm SHA256).Hash.ToLowerInvariant()

if (-not [string]::Equals($outputFull, $inputFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    $parent = Split-Path -Parent $outputFull
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Copy-Item -LiteralPath $inputFull -Destination $outputFull -Force
}

$signArgs = @("sign", "/fd", "SHA256", "/tr", $TimestampUrl, "/td", "SHA256")
if (-not [string]::IsNullOrWhiteSpace($Thumbprint)) {
    $signArgs += @("/sha1", ($Thumbprint -replace '\s+', '').ToUpperInvariant())
} else {
    $pfxFull = Resolve-FullPath $PfxPath
    $signArgs += @("/f", $pfxFull, "/p", $PfxPassword)
}
$signArgs += $outputFull

& $SignToolPath @signArgs
if ($LASTEXITCODE -ne 0) {
    throw "signtool sign failed with exit code $LASTEXITCODE"
}

if (-not $NoVerify) {
    & $SignToolPath verify /pa /all $outputFull
    if ($LASTEXITCODE -ne 0) {
        throw "signtool verify failed with exit code $LASTEXITCODE"
    }
}

$signedHash = (Get-FileHash -LiteralPath $outputFull -Algorithm SHA256).Hash.ToLowerInvariant()
$metadataPath = "$outputFull.sha256.txt"
@(
    "unsigned_sha256 $unsignedHash"
    "signed_sha256 $signedHash"
    "timestamp_url $TimestampUrl"
    "artifact $outputFull"
) | Set-Content -LiteralPath $metadataPath -Encoding ASCII

Write-Output $outputFull
Write-Output $metadataPath
