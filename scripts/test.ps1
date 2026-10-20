param(
    [ValidateSet('debug', 'release', 'shipping', 'debug-asan', 'ci-windows')]
    [string]$Preset = 'debug'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

. (Join-Path $PSScriptRoot 'bootstrap.ps1') -Quiet

& ctest --preset $Preset --output-on-failure
exit $LASTEXITCODE
