param(
    [switch]$Diagnose,
    [ValidateSet('x64', 'arm64')][string]$Arch = 'x64',
    [string]$Toolchain = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path 'build' | Out-Null
    $version = & "$PSScriptRoot/version.ps1"
    if ($Toolchain) {
        $triple = if ($Arch -eq 'arm64') { 'aarch64' } else { 'x86_64' }
        $compiler = Join-Path $Toolchain "bin/$triple-w64-mingw32-clang.exe"
        $windres = Join-Path $Toolchain "bin/$triple-w64-mingw32-windres.exe"
    } else {
        if ($Arch -eq 'arm64') { throw 'Windows ARM64 requires -Toolchain pointing to an LLVM-MinGW installation.' }
        $compiler = (Get-Command gcc -ErrorAction Stop).Source
        $windres = (Get-Command windres -ErrorAction Stop).Source
    }
    $target = & $compiler -dumpmachine
    if ($LASTEXITCODE -ne 0 -or ($Arch -eq 'x64' -and $target -notmatch '^x86_64-') -or
        ($Arch -eq 'arm64' -and $target -notmatch '^aarch64-')) { throw "Compiler target '$target' does not match $Arch." }
    & $windres -I windows windows/AudioTrace.rc -O coff -o build/version.o
    if ($LASTEXITCODE -ne 0) { throw 'Windows version resource compilation failed.' }
    $sources = @('windows/main.c', 'common/audio_events.c', 'build/version.o')
    $libraries = @('-lole32', '-luuid', '-lshell32', '-luser32', '-lgdi32', '-lm')
    # Keep system DLL imports; statically link compiler support so no runtime ships beside the EXE.
    $flags = @('-std=c11', '-Os', '-flto', '-ffunction-sections', '-fdata-sections',
        '-Wl,--gc-sections', '-s', '-Wall', '-Wextra', '-Werror', '-municode', '-static')
    & $compiler @flags -mwindows @sources @libraries -o build/AudioTrace.exe
    if ($LASTEXITCODE -ne 0) { throw 'Windows app compilation failed.' }
    & "$PSScriptRoot/verify-build.ps1" -Arch $Arch -Version $version
    if ($Diagnose) {
        # Console subsystem lets PowerShell wait for diagnostic output.
        & $compiler @flags @sources @libraries -o build/AudioTrace-diagnose.exe
        if ($LASTEXITCODE -ne 0) { throw 'Diagnostic compilation failed.' }
        & .\build\AudioTrace-diagnose.exe --diagnose
        if ($LASTEXITCODE -ne 0) { throw 'Audio discovery diagnostic failed.' }
    }
    Write-Host 'Built build/AudioTrace.exe. Run it to show the tray icon.'
} finally {
    Pop-Location
}
