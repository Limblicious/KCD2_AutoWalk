param(
    [string]$GameRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepositoryRoot
$resolvedGameRoot = Resolve-Kcd2GameRoot -GameRoot $GameRoot
$dataDir = Join-Path $resolvedGameRoot "Data"

Add-Type -AssemblyName System.IO.Compression.FileSystem

$work = Join-Path $env:TEMP "kcd2_autowalk_moddata"
if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $work | Out-Null

$profileIn = $null
$helpIn = $null
$pakName = $null
foreach ($pak in Get-ChildItem -LiteralPath $dataDir -Filter "*.pak" -File) {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($pak.FullName)
    try {
        $profileEntry = $zip.Entries | Where-Object { $_.FullName -eq "Libs/Config/defaultProfile.xml" } | Select-Object -First 1
        $helpEntry = $zip.Entries | Where-Object { $_.FullName -eq "Libs/Config/defaultActionHelp.xml" } | Select-Object -First 1
        if ($profileEntry -or $helpEntry) {
            $pakName = $pak.Name
            if ($profileEntry) {
                $profileIn = Join-Path $work "defaultProfile.xml"
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($profileEntry, $profileIn, $true)
            }
            if ($helpEntry) {
                $helpIn = Join-Path $work "defaultActionHelp.xml"
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($helpEntry, $helpIn, $true)
            }
            break
        }
    } finally {
        $zip.Dispose()
    }
}

if (-not $profileIn -or -not $helpIn) {
    throw "Vanilla defaultProfile.xml / defaultActionHelp.xml not found in game data paks."
}

$dataOut = Join-Path $repoRoot "package\kcd_autowalk\data"
New-Item -ItemType Directory -Force -Path $dataOut | Out-Null
$pakOut = Join-Path $dataOut "AutoWalkData.pak"

& python (Join-Path $PSScriptRoot "patch_mod_data.py") $profileIn $helpIn $pakOut
if ($LASTEXITCODE -ne 0) { throw "patch_mod_data.py failed." }

Write-Host "Mod data pak: $pakOut (from $pakName)"
