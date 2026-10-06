param(
    [string]$GameRoot,
    [switch]$Refresh
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepositoryRoot
$resolvedGameRoot = Resolve-Kcd2GameRoot -GameRoot $GameRoot
$binaryDir = Get-Kcd2BinaryDirectory -GameRoot $resolvedGameRoot
$sourceDll = Join-Path $binaryDir "WHGame.dll"

if (-not (Test-Path -LiteralPath $sourceDll -PathType Leaf)) {
    throw "WHGame.dll was not found at '$sourceDll'."
}

$reRoot = Join-Path $repoRoot ".re"
$inputRoot = Join-Path $reRoot "input"
$targetDll = Join-Path $inputRoot "WHGame.dll"
$metadataPath = Join-Path $reRoot "target.json"

New-Item -ItemType Directory -Force -Path $inputRoot | Out-Null

if ((Test-Path -LiteralPath $targetDll) -and -not $Refresh) {
    Write-Host "RE copy already exists: $targetDll"
    Write-Host "Use -Refresh to replace the local RE copy from the installed game."
} else {
    Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
    Write-Host "Copied WHGame.dll to local RE workspace."
}

$sourceHash = (Get-FileHash -LiteralPath $sourceDll -Algorithm SHA256).Hash
$targetHash = (Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash
if ($sourceHash -ne $targetHash) {
    throw "RE copy hash mismatch. Installed game was NOT modified."
}

$version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($sourceDll)
$addressRoot = Join-Path $resolvedGameRoot "KCSE\addresslib"
$addressFiles = @(
    Get-ChildItem -LiteralPath $addressRoot -Filter "kcd_addresslib_*.bin" -File -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty Name
)

$metadata = [ordered]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    source_path = $sourceDll
    re_copy_path = $targetDll
    sha256 = $sourceHash
    file_version = $version.FileVersion
    product_version = $version.ProductVersion
    file_size = (Get-Item -LiteralPath $sourceDll).Length
    address_library_files = $addressFiles
    expected_project_runtime = "KCD2 Steam 1.5.6 / release_1_5-15693"
    note = "RE copy only. Never patch the installed WHGame.dll."
}

$metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $metadataPath -Encoding UTF8

Write-Host ""
Write-Host "Reverse-engineering target prepared:"
Write-Host "  Copy:    $targetDll"
Write-Host "  SHA256:  $sourceHash"
Write-Host "  Version: $($version.FileVersion)"
Write-Host "  Meta:    $metadataPath"
Write-Host ""
Write-Host "The installed WHGame.dll was read and copied only; it was not modified."
