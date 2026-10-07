param(
    [string]$GhidraRoot = $env:GHIDRA_HOME,
    [switch]$Rebuild,
    [switch]$SkipSeeds
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$repoRoot = Get-RepositoryRoot
if (-not $GhidraRoot) {
    throw "GHIDRA_HOME is not set. See docs/GHIDRA_SETUP.md."
}

$headless = Join-Path $GhidraRoot "support\analyzeHeadless.bat"
if (-not (Test-Path -LiteralPath $headless)) {
    throw "Ghidra headless launcher not found at '$headless'."
}

$target = Join-Path $repoRoot ".re\input\WHGame.dll"
if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
    throw "RE target copy not found. Run scripts/prepare-re.ps1 first."
}

$projectRoot = Join-Path $repoRoot ".re\ghidra"
$projectName = "KCD2_AutoWalk_RE"
$projectFile = Join-Path $projectRoot "$projectName.gpr"
$scriptPath = Join-Path $repoRoot "re\ghidra_scripts"

if ($Rebuild -and (Test-Path -LiteralPath $projectRoot)) {
    $expected = [System.IO.Path]::GetFullPath((Join-Path $repoRoot ".re\ghidra")).TrimEnd('\')
    $actual = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\')
    if (-not [string]::Equals($expected, $actual, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to rebuild unexpected path '$actual'."
    }
    Remove-Item -LiteralPath $projectRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $projectRoot | Out-Null

$args = @($projectRoot, $projectName)
if (Test-Path -LiteralPath $projectFile) {
    $args += @("-process", "WHGame.dll")
} else {
    $args += @("-import", $target)
}

$args += @("-scriptPath", $scriptPath)
if (-not $SkipSeeds) {
    $args += @("-postScript", "ApplyAutoWalkSeeds.java")
}

Write-Host "Running Ghidra headless analysis..."
Write-Host "  Target:  $target"
Write-Host "  Project: $projectFile"

& $headless @args
if ($LASTEXITCODE -ne 0) {
    throw "Ghidra headless analysis failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "Ghidra project ready: $projectFile"
Write-Host "Open it in Ghidra, enable GhidraMCP, and follow docs/RE_WORKSTATION.md."
