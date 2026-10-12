param([switch]$Diagnose)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path 'build' | Out-Null
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $gcc) {
        throw 'Install a MinGW-w64 GCC toolchain and put gcc on PATH, then rerun this script.'
    }
    $sources = @('windows/main.c', 'common/audio_events.c')
    $libraries = @('-lole32', '-luuid', '-lshell32', '-luser32', '-lgdi32', '-lm')
    & $gcc.Source -std=c11 -O2 -Wall -Wextra -Werror -municode -mwindows -static $sources $libraries -o build/AudioTrace.exe
    if ($LASTEXITCODE -ne 0) { throw 'Windows app compilation failed.' }
    if ($Diagnose) {
        # Console subsystem lets PowerShell wait for diagnostic output.
        & $gcc.Source -std=c11 -O2 -Wall -Wextra -Werror -municode -static $sources $libraries -o build/AudioTrace-diagnose.exe
        if ($LASTEXITCODE -ne 0) { throw 'Diagnostic compilation failed.' }
        & .\build\AudioTrace-diagnose.exe --diagnose
        if ($LASTEXITCODE -ne 0) { throw 'Audio discovery diagnostic failed.' }
    }
    Write-Host 'Built build/AudioTrace.exe. Run it to show the tray icon.'
} finally {
    Pop-Location
}
