param(
    [ValidateSet('debug', 'release', 'shipping', 'debug-asan', 'ci-windows')]
    [string]$Preset = 'debug',
    [switch]$Clean,
    [switch]$Test,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

. (Join-Path $PSScriptRoot 'bootstrap.ps1') -Quiet

if ($Clean) {
    $buildDir = Join-Path $root "build/$Preset"
    if (Test-Path $buildDir) {
        Remove-Item -Recurse -Force $buildDir
    }
}

& cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $ConfigureOnly) {
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($Test) {
    & ctest --preset $Preset --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
