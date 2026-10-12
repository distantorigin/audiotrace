param([string]$Header = (Join-Path $PSScriptRoot 'version.h'))
$ErrorActionPreference = 'Stop'
$text = Get-Content $Header -Raw
$parts = foreach ($component in @('MAJOR', 'MINOR', 'PATCH')) {
    $definitions = [regex]::Matches($text, "(?m)^#define AT_VERSION_$component (0|[1-9][0-9]*)\s*$")
    if ($definitions.Count -ne 1) { throw "Expected one numeric AT_VERSION_$component in $Header." }
    $value = [int]$definitions[0].Groups[1].Value
    if ($value -gt 65535) { throw 'Windows version components must fit in 16 bits.' }
    $value
}
$parts -join '.'
