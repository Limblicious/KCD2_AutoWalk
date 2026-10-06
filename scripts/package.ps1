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
Copy-Item -LiteralPath (Join-Path $repoRoot "package\kcd_autowalk\mod.manifest") -Destination $modRoot
Copy-Item -LiteralPath (Join-Path $repoRoot "package\kcd_autowalk\mod.cfg") -Destination $modRoot
Copy-Item -LiteralPath $dll -Destination (Join-Path $plugins "KCD2_AutoWalk.dll")

$zip = Join-Path $distRoot "KCD2_AutoWalk-$Version.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath $modRoot -DestinationPath $zip -CompressionLevel Optimal

Write-Host "Packaged mod: $modRoot"
Write-Host "Release ZIP:   $zip"
