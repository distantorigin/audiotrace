param([switch]$Diagnose)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path 'build' | Out-Null
    $version = & "$PSScriptRoot/version.ps1"
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $gcc) {
        throw 'Install a MinGW-w64 GCC toolchain and put gcc on PATH, then rerun this script.'
    }
    & windres -I windows windows/AudioTrace.rc -O coff -o build/version.o
    if ($LASTEXITCODE -ne 0) { throw 'Windows version resource compilation failed.' }
    $sources = @('windows/main.c', 'common/audio_events.c', 'build/version.o')
    $libraries = @('-lole32', '-luuid', '-lshell32', '-luser32', '-lgdi32', '-lm')
    & $gcc.Source -std=c11 -Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -s -Wall -Wextra -Werror -municode -mwindows -static $sources $libraries -o build/AudioTrace.exe
    if ($LASTEXITCODE -ne 0) { throw 'Windows app compilation failed.' }
    & "$PSScriptRoot/verify-build.ps1" -Version $version
    if ($Diagnose) {
        # Console subsystem lets PowerShell wait for diagnostic output.
        & $gcc.Source -std=c11 -Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -s -Wall -Wextra -Werror -municode -static $sources $libraries -o build/AudioTrace-diagnose.exe
        if ($LASTEXITCODE -ne 0) { throw 'Diagnostic compilation failed.' }
        & .\build\AudioTrace-diagnose.exe --diagnose
        if ($LASTEXITCODE -ne 0) { throw 'Audio discovery diagnostic failed.' }
    }
    Write-Host 'Built build/AudioTrace.exe. Run it to show the tray icon.'
} finally {
    Pop-Location
}
