param([string]$GameRoot,[switch]$Confirm)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$resolvedGameRoot = Resolve-Kcd2GameRoot -GameRoot $GameRoot
$target = Get-AutoWalkModRoot -GameRoot $resolvedGameRoot
Assert-SafeAutoWalkModRoot -GameRoot $resolvedGameRoot -Target $target

if (-not (Test-Path -LiteralPath $target)) { Write-Host "AutoWalk is not installed."; exit 0 }
if (-not $Confirm) { throw "Refusing uninstall without -Confirm. Target: '$target'." }

Remove-Item -LiteralPath $target -Recurse -Force
Write-Host "Removed exactly: $target"
Write-Host "KCSE, Address Library, game files, and other mods were not touched."
