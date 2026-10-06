param([string]$LibKCD2Root,[switch]$SkipVcpkg)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepositoryRoot
$pinnedCommit = "10d20f28faba462c4bf98a01abb48225cc51bb91"
if (-not $LibKCD2Root) { $LibKCD2Root = Join-Path $repoRoot ".deps\libKCD2" }

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "git was not found on PATH." }

$freshClone = $false
if (-not (Test-Path -LiteralPath (Join-Path $LibKCD2Root ".git"))) {
    $freshClone = $true
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LibKCD2Root) | Out-Null
    & git clone --filter=blob:none --no-checkout https://github.com/JerryYOJ/libKCD2.git $LibKCD2Root
    if ($LASTEXITCODE -ne 0) { throw "git clone libKCD2 failed." }
}

Push-Location $LibKCD2Root
try {
    if (-not $freshClone) {
        if (& git status --porcelain) {
            throw "Dependency checkout has local modifications; refusing to overwrite it."
        }
    }

    $current = (& git rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -ne 0 -or $current -ne $pinnedCommit) {
        & git fetch --depth 1 origin $pinnedCommit
        if ($LASTEXITCODE -ne 0) { throw "Failed to fetch pinned libKCD2 commit." }

        & git checkout --detach $pinnedCommit
        if ($LASTEXITCODE -ne 0) { throw "Failed to checkout pinned libKCD2 commit." }
    }

    $verified = (& git rev-parse HEAD).Trim()
    if ($verified -ne $pinnedCommit) { throw "libKCD2 pin verification failed." }

    if (& git status --porcelain) {
        throw "Pinned libKCD2 checkout is unexpectedly dirty after verification."
    }
} finally { Pop-Location }

Write-Host "libKCD2: $pinnedCommit"

if (-not $SkipVcpkg) {
    if (-not $env:VCPKG_ROOT) { throw "VCPKG_ROOT is not set." }
    $vcpkgExe = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
    if (-not (Test-Path -LiteralPath $vcpkgExe)) { throw "vcpkg.exe not found at '$vcpkgExe'." }
    Push-Location $repoRoot
    try {
        & $vcpkgExe install --triplet x64-windows-static-md
        if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed." }
    } finally { Pop-Location }
}

Write-Host "Bootstrap complete."
