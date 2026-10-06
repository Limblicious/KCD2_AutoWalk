param([string]$GameRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$resolvedGameRoot = Resolve-Kcd2GameRoot -GameRoot $GameRoot
$binaryDir = Get-Kcd2BinaryDirectory -GameRoot $resolvedGameRoot
$target = Get-AutoWalkModRoot -GameRoot $resolvedGameRoot

$kcse = Join-Path $binaryDir "dinput8.dll"
$addressRoot = Join-Path $resolvedGameRoot "KCSE\addresslib"
$addressFiles = @(Get-ChildItem -LiteralPath $addressRoot -Filter "kcd_addresslib_*.bin" -File -ErrorAction SilentlyContinue)
$plugin = Join-Path $target "KCSE\Plugins\KCD2_AutoWalk.dll"
$manifest = Join-Path $target "mod.manifest"
$log = Join-Path $target "KCD2_AutoWalk.log"

Write-Host "KCD2 root:      $resolvedGameRoot"
Write-Host "Binary dir:     $binaryDir"
Write-Host "KCSE dinput8:   $(Test-Path -LiteralPath $kcse)"
Write-Host "Address tables: $($addressFiles.Count)"
foreach ($file in $addressFiles) { Write-Host "  - $($file.Name)" }
Write-Host "Manifest:       $(Test-Path -LiteralPath $manifest)"
Write-Host "Plugin DLL:     $(Test-Path -LiteralPath $plugin)"
Write-Host "Plugin log:     $(Test-Path -LiteralPath $log)"

if (Test-Path -LiteralPath $log) {
    Write-Host ""
    Get-Content -LiteralPath $log -Tail 20
}
