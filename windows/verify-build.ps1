param(
    [string]$Path = 'build/AudioTrace.exe',
    [ValidateSet('x64', 'arm64')][string]$Arch = 'x64',
    [string]$Version = (& "$PSScriptRoot/version.ps1")
)
$ErrorActionPreference = 'Stop'
$resolvedPath = (Resolve-Path -LiteralPath $Path).ProviderPath
$binary = [IO.File]::ReadAllBytes($resolvedPath)
$pe = [BitConverter]::ToInt32($binary, 0x3c)
if ([BitConverter]::ToUInt32($binary, $pe) -ne 0x4550) { throw 'Invalid PE signature.' }
$expectedMachine = if ($Arch -eq 'arm64') { 0xaa64 } else { 0x8664 }
if ([BitConverter]::ToUInt16($binary, $pe + 4) -ne $expectedMachine) { throw "Expected $Arch executable." }
if ([BitConverter]::ToUInt16($binary, $pe + 24) -ne 0x20b) { throw 'Expected PE32+ executable.' }
if ([BitConverter]::ToUInt16($binary, $pe + 24 + 68) -ne 2) { throw 'Expected Windows GUI subsystem.' }
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($resolvedPath)
if ($info.ProductVersion -ne $Version -or $info.FileVersion -ne $Version) {
    throw "Expected version $Version in file and product metadata; got '$($info.FileVersion)' / '$($info.ProductVersion)'."
}
Write-Host "Verified $Path ($Arch, version $Version, $($binary.Length) bytes)."
