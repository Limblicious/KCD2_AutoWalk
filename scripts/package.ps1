param(
    [ValidateSet("Debug","Release","RelWithDebInfo")][string]$Configuration = "Release",
    [string]$Version = "0.1.0-dev"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepositoryRoot
$dll = Join-Path $repoRoot "build\bin\$Configuration\KCD2_AutoWalk.dll"
if (-not (Test-Path -LiteralPath $dll)) { throw "DLL not found. Build first." }

$distRoot = Join-Path $repoRoot "dist"
$modRoot = Join-Path $distRoot $script:ModFolderName
$plugins = Join-Path $modRoot "KCSE\Plugins"

if (Test-Path -LiteralPath $modRoot) {
    $expectedParent = [System.IO.Path]::GetFullPath($distRoot).TrimEnd('\')
    $actualParent = [System.IO.Path]::GetFullPath((Split-Path -Parent $modRoot)).TrimEnd('\')
    if ($actualParent -ne $expectedParent) { throw "Refusing package cleanup: unexpected dist path." }
    Remove-Item -LiteralPath $modRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $plugins | Out-Null

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

# The manifest version follows the requested package version (the source
# template keeps its development marker).
$manifestSrc = Join-Path $repoRoot "package\kcd_autowalk\mod.manifest"
$manifestXml = Get-Content -LiteralPath $manifestSrc -Raw
$manifestXml = [System.Text.RegularExpressions.Regex]::Replace(
    $manifestXml, '(?s)(<info>.*?<version>)[^<]*(</version>)', ('${1}' + $Version + '${2}'))
[System.IO.File]::WriteAllText((Join-Path $modRoot "mod.manifest"), $manifestXml, $utf8NoBom)

# Configuration-specific settings: Release ships with development logging
# disabled; Debug keeps the diagnostics.
$cfgSrc = Join-Path $repoRoot "package\kcd_autowalk\mod.cfg"
$cfg = Get-Content -LiteralPath $cfgSrc -Raw
if ($Configuration -eq "Release") {
    $cfg = [System.Text.RegularExpressions.Regex]::Replace(
        $cfg, 'kcse_autowalk_debug\s+[0-9]+', 'kcse_autowalk_debug 0')
}
[System.IO.File]::WriteAllText((Join-Path $modRoot "mod.cfg"), $cfg, $utf8NoBom)

Copy-Item -LiteralPath $dll -Destination (Join-Path $plugins "KCD2_AutoWalk.dll")

# Mod data pak (patched vanilla action profile + help rows).
& (Join-Path $PSScriptRoot "build-mod-data.ps1")
if ($LASTEXITCODE -ne 0) { throw "build-mod-data.ps1 failed." }
$dataSrc = Join-Path $repoRoot "package\kcd_autowalk\data"
if (Test-Path -LiteralPath $dataSrc) {
    Copy-Item -LiteralPath $dataSrc -Destination $modRoot -Recurse
}
$locSrc = Join-Path $repoRoot "package\kcd_autowalk\Localization"
if (Test-Path -LiteralPath $locSrc) {
    Copy-Item -LiteralPath $locSrc -Destination $modRoot -Recurse
}

$zip = Join-Path $distRoot "KCD2_AutoWalk-$Version.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath $modRoot -DestinationPath $zip -CompressionLevel Optimal

Write-Host "Packaged mod: $modRoot"
Write-Host "Release ZIP:   $zip"
