param(
    [string]$GhidraRoot = $env:GHIDRA_HOME,
    [switch]$CheckMcp
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $GhidraRoot) {
    throw "GHIDRA_HOME is not set. Point it at the extracted Ghidra 12.1.4 directory."
}

$GhidraRoot = [System.IO.Path]::GetFullPath($GhidraRoot)
$headless = Join-Path $GhidraRoot "support\analyzeHeadless.bat"
$launcher = Join-Path $GhidraRoot "ghidraRun.bat"

if (-not (Test-Path -LiteralPath $headless)) {
    throw "Ghidra analyzeHeadless.bat not found at '$headless'."
}
if (-not (Test-Path -LiteralPath $launcher)) {
    throw "Ghidra ghidraRun.bat not found at '$launcher'."
}

Write-Host "Ghidra root:     $GhidraRoot"
Write-Host "Headless:        FOUND"
Write-Host "GUI launcher:    FOUND"

$java = Get-Command java -ErrorAction SilentlyContinue
if (-not $java) {
    throw "java was not found on PATH. Ghidra 12.1.4 requires a full JDK 21."
}

$javaVersionOutput = (& java -version 2>&1 | Select-Object -First 1).ToString()
Write-Host "Java:            $javaVersionOutput"
if ($javaVersionOutput -notmatch '"21(?:\.|")') {
    Write-Warning "Expected JDK 21 for Ghidra 12.1.4."
}

$opencode = Get-Command opencode -ErrorAction SilentlyContinue
if ($opencode) {
    $opencodeVersion = (& opencode --version 2>$null | Select-Object -First 1)
    Write-Host "OpenCode:        FOUND $opencodeVersion"
} else {
    Write-Warning "OpenCode not found on PATH."
}

if ($CheckMcp) {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $async = $client.BeginConnect("127.0.0.1", 8080, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne(2000)) { throw "timeout" }
        $client.EndConnect($async)
        Write-Host "GhidraMCP:       LISTENING on 127.0.0.1:8080"
    } catch {
        Write-Warning "Nothing is listening on 127.0.0.1:8080. Start Tools > GhidraMCP > Start MCP Server."
    } finally {
        $client.Close()
    }
}

Write-Host ""
Write-Host "Expected RE stack:"
Write-Host "  Ghidra:    12.1.4"
Write-Host "  Java:      JDK 21"
Write-Host "  GhidraMCP: v0.9.0"
