param(
    [ValidateSet("Debug","Release","RelWithDebInfo")][string]$Configuration = "Debug",
    [string]$LibKCD2Root,
    [switch]$SkipBootstrap,
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepositoryRoot
$buildDir = Join-Path $repoRoot "build"
if (-not $LibKCD2Root) { $LibKCD2Root = Join-Path $repoRoot ".deps\libKCD2" }

if (-not $SkipBootstrap) { & (Join-Path $PSScriptRoot "bootstrap.ps1") -LibKCD2Root $LibKCD2Root }
if (-not $env:VCPKG_ROOT) { throw "VCPKG_ROOT is not set." }

$toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path -LiteralPath $toolchain)) { throw "vcpkg CMake toolchain not found." }

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    $expected = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "build"))
    $actual = [System.IO.Path]::GetFullPath($buildDir)
    if ($actual -ne $expected) { throw "Refusing clean: unexpected build path." }
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}

$configureArgs = @(
    "-S",$repoRoot,
    "-B",$buildDir,
    "-G","Visual Studio 17 2022",
    "-A","x64",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md",
    "-DLIBKCD2_ROOT=$LibKCD2Root"
)
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

& cmake --build $buildDir --config $Configuration --target KCD2_AutoWalk -- /m
if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }

$dll = Join-Path $buildDir "bin\$Configuration\KCD2_AutoWalk.dll"
if (-not (Test-Path -LiteralPath $dll)) { throw "Build succeeded but DLL was not found at '$dll'." }
Write-Host "Built: $dll"
