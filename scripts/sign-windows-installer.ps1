param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [string]$OutputPath,
    [string]$PfxPath = $env:VIPER_WINDOWS_SIGN_PFX,
    [string]$PfxPassword = $env:VIPER_WINDOWS_SIGN_PASSWORD,
    [string]$TimestampUrl = $(if ($env:VIPER_WINDOWS_TIMESTAMP_URL) { $env:VIPER_WINDOWS_TIMESTAMP_URL } else { "http://timestamp.digicert.com" }),
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
if ([string]::IsNullOrWhiteSpace($PfxPath)) {
    throw "PFX path is required. Pass -PfxPath or set VIPER_WINDOWS_SIGN_PFX."
}
if (-not (Test-Path -LiteralPath $PfxPath -PathType Leaf)) {
    throw "PFX file not found: $PfxPath"
}
if ($null -eq $PfxPassword) {
    throw "PFX password is required. Pass -PfxPassword or set VIPER_WINDOWS_SIGN_PASSWORD."
}

$inputFull = Resolve-FullPath $InputPath
$pfxFull = Resolve-FullPath $PfxPath
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

& $SignToolPath sign /fd SHA256 /tr $TimestampUrl /td SHA256 /f $pfxFull /p $PfxPassword $outputFull
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
