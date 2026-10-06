Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script:Kcd2SteamAppId = "1771300"
$script:ModFolderName = "kcd_autowalk"

function Get-RepositoryRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Test-Kcd2GameRoot {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return $false }
    $binRoot = Join-Path $Path "Bin"
    if (-not (Test-Path -LiteralPath $binRoot -PathType Container)) { return $false }

    $bin = Get-ChildItem -LiteralPath $binRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "Win64MasterMaster*PGO" } |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "WHGame.dll") } |
        Select-Object -First 1
    return $null -ne $bin
}

function Get-Kcd2BinaryDirectory {
    param([Parameter(Mandatory=$true)][string]$GameRoot)
    $bin = Get-ChildItem -LiteralPath (Join-Path $GameRoot "Bin") -Directory -ErrorAction Stop |
        Where-Object { $_.Name -like "Win64MasterMaster*PGO" } |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "WHGame.dll") } |
        Select-Object -First 1
    if ($null -eq $bin) { throw "Could not find WHGame.dll under '$GameRoot\Bin'." }
    return $bin.FullName
}

function Get-SteamLibraryPaths {
    $paths = New-Object System.Collections.Generic.List[string]
    $steamPath = $null
    try { $steamPath = (Get-ItemProperty -LiteralPath "HKCU:\Software\Valve\Steam" -ErrorAction Stop).SteamPath } catch {}

    if ($steamPath) {
        $paths.Add([System.IO.Path]::GetFullPath($steamPath))
        $vdf = Join-Path $steamPath "steamapps\libraryfolders.vdf"
        if (Test-Path -LiteralPath $vdf) {
            foreach ($line in Get-Content -LiteralPath $vdf) {
                if ($line -match '"path"\s+"([^"]+)"') {
                    $candidate = $Matches[1] -replace '\\\\', '\'
                    if ($candidate) { $paths.Add([System.IO.Path]::GetFullPath($candidate)) }
                }
            }
        }
    }
    return $paths | Sort-Object -Unique
}

function Resolve-Kcd2GameRoot {
    param([string]$GameRoot)

    $candidates = New-Object System.Collections.Generic.List[string]
    if ($GameRoot) { $candidates.Add($GameRoot) }
    if ($env:KCD2_GAME_ROOT) { $candidates.Add($env:KCD2_GAME_ROOT) }

    foreach ($library in Get-SteamLibraryPaths) {
        $manifest = Join-Path $library "steamapps\appmanifest_$($script:Kcd2SteamAppId).acf"
        if (Test-Path -LiteralPath $manifest) {
            $candidates.Add((Join-Path $library "steamapps\common\KingdomComeDeliverance2"))
        }
    }

    $candidates.Add("C:\Program Files (x86)\Steam\steamapps\common\KingdomComeDeliverance2")

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if ($candidate -and (Test-Kcd2GameRoot -Path $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Could not locate KCD2. Pass -GameRoot or set KCD2_GAME_ROOT. No files were modified."
}

function Get-AutoWalkModRoot {
    param([Parameter(Mandatory=$true)][string]$GameRoot)
    return Join-Path (Join-Path $GameRoot "Mods") $script:ModFolderName
}

function Assert-SafeAutoWalkModRoot {
    param([Parameter(Mandatory=$true)][string]$GameRoot,[Parameter(Mandatory=$true)][string]$Target)

    if (-not (Test-Kcd2GameRoot -Path $GameRoot)) { throw "Refusing operation: invalid KCD2 root." }

    $expected = [System.IO.Path]::GetFullPath((Get-AutoWalkModRoot -GameRoot $GameRoot)).TrimEnd('\')
    $actual = [System.IO.Path]::GetFullPath($Target).TrimEnd('\')

    if (-not [string]::Equals($expected,$actual,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing operation: target '$actual' is not exactly '$expected'."
    }
    if ((Split-Path -Leaf $actual) -ne $script:ModFolderName) { throw "Refusing operation: unexpected target leaf." }
    if ((Split-Path -Leaf (Split-Path -Parent $actual)) -ne "Mods") { throw "Refusing operation: target parent is not Mods." }
}
