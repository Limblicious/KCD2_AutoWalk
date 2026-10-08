param(
    [ValidateSet("Debug","Release","RelWithDebInfo")][string]$Configuration = "Release",
    [string]$Version = "0.1.0"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepositoryRoot

# Build the normal package first. The compatibility pak is derived from its
# current-game AutoWalkData.pak, so the base release remains unchanged.
& (Join-Path $PSScriptRoot "package.ps1") -Configuration $Configuration -Version $Version
if ($LASTEXITCODE -ne 0) { throw "package.ps1 failed." }

$sourcePak = Join-Path $repoRoot "dist\kcd_autowalk\data\AutoWalkData.pak"
if (-not (Test-Path -LiteralPath $sourcePak -PathType Leaf)) {
    throw "AutoWalkData.pak not found after packaging."
}

$distRoot = Join-Path $repoRoot "dist"
$modName = "kcd_autowalk_usii_compat"
$modRoot = Join-Path $distRoot $modName
if (Test-Path -LiteralPath $modRoot) {
    $expectedParent = [System.IO.Path]::GetFullPath($distRoot).TrimEnd('\')
    $actualParent = [System.IO.Path]::GetFullPath((Split-Path -Parent $modRoot)).TrimEnd('\')
    if ($actualParent -ne $expectedParent) { throw "Refusing compatibility package cleanup: unexpected dist path." }
    Remove-Item -LiteralPath $modRoot -Recurse -Force
}

$dataRoot = Join-Path $modRoot "data"
New-Item -ItemType Directory -Force -Path $dataRoot | Out-Null

$templateRoot = Join-Path $repoRoot "package\$modName"
$manifestXml = Get-Content -LiteralPath (Join-Path $templateRoot "mod.manifest") -Raw
$manifestXml = [System.Text.RegularExpressions.Regex]::Replace(
    $manifestXml, '(?s)(<info>.*?<version>)[^<]*(</version>)', ('${1}' + $Version + '${2}'))
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path $modRoot "mod.manifest"), $manifestXml, $utf8NoBom)
Copy-Item -LiteralPath (Join-Path $templateRoot "mod.cfg") -Destination $modRoot

$compatPak = Join-Path $dataRoot "AutoWalkUSIICompat.pak"
& python (Join-Path $PSScriptRoot "patch-usii-compat.py") $sourcePak $compatPak
if ($LASTEXITCODE -ne 0) { throw "patch-usii-compat.py failed." }

$zip = Join-Path $distRoot "KCD2_AutoWalk-USII-Compat-$Version.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath $modRoot -DestinationPath $zip -CompressionLevel Optimal

Write-Host "Compatibility mod: $modRoot"
Write-Host "Compatibility ZIP: $zip"
