param(
    [ValidateSet("Debug","Release","RelWithDebInfo")][string]$Configuration = "Debug",
    [string]$GameRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration
& (Join-Path $PSScriptRoot "package.ps1") -Configuration $Configuration
& (Join-Path $PSScriptRoot "install.ps1") -Configuration $Configuration -GameRoot $GameRoot -SkipPackage
& (Join-Path $PSScriptRoot "diagnose.ps1") -GameRoot $GameRoot
