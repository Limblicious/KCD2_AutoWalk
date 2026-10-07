param(
    [ValidateSet("Debug","Release","RelWithDebInfo")][string]$Configuration = "Debug",
    [string]$GameRoot,
    [switch]$SkipPackage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepositoryRoot
$resolvedGameRoot = Resolve-Kcd2GameRoot -GameRoot $GameRoot
$binaryDir = Get-Kcd2BinaryDirectory -GameRoot $resolvedGameRoot

$kcseLoader = Join-Path $binaryDir "dinput8.dll"
if (-not (Test-Path -LiteralPath $kcseLoader)) { throw "KCSE loader not found. No files were modified." }

$addressLibRoot = Join-Path $resolvedGameRoot "KCSE\addresslib"
$addressFiles = @(Get-ChildItem -LiteralPath $addressLibRoot -Filter "kcd_addresslib_*.bin" -File -ErrorAction SilentlyContinue)
if ($addressFiles.Count -eq 0) { throw "KCSE Address Library not found. No files were modified." }

if (-not $SkipPackage) { & (Join-Path $PSScriptRoot "package.ps1") -Configuration $Configuration }

$source = Join-Path $repoRoot "dist\$($script:ModFolderName)"
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Packaged mod not found." }

$target = Get-AutoWalkModRoot -GameRoot $resolvedGameRoot
Assert-SafeAutoWalkModRoot -GameRoot $resolvedGameRoot -Target $target

New-Item -ItemType Directory -Force -Path (Join-Path $target "KCSE\Plugins") | Out-Null
Copy-Item -LiteralPath (Join-Path $source "mod.manifest") -Destination $target -Force
Copy-Item -LiteralPath (Join-Path $source "mod.cfg") -Destination $target -Force
Copy-Item -LiteralPath (Join-Path $source "KCSE\Plugins\KCD2_AutoWalk.dll") -Destination (Join-Path $target "KCSE\Plugins\KCD2_AutoWalk.dll") -Force
if (Test-Path -LiteralPath (Join-Path $source "data")) {
    Copy-Item -LiteralPath (Join-Path $source "data") -Destination $target -Recurse -Force
}

Write-Host "Installed only: $target"
Write-Host "KCSE and Address Library were verified but not modified."
