param(
    [switch]$Quiet
)

# Locate Visual Studio 2022 first (to match the CI image), then fall back to
# the newest installation. Dot-source this file so the environment changes
# stay in the caller's session:
#
#   . .\scripts\bootstrap.ps1

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio 2022 with the Desktop C++ workload."
}

$vsPath = & $vswhere -latest -version '[17.0,18.0)' -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $vsPath) {
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
}

if (-not $vsPath) {
    throw 'No Visual Studio installation with the MSVC x64 toolset was found.'
}

$devShell = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
if (-not (Test-Path $devShell)) {
    throw "Visual Studio developer shell module not found at $devShell"
}

Import-Module $devShell
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

# The Visual Studio installation ships CMake and Ninja, but does not always
# put them on PATH. Prefer them when system-wide tools are absent.
function Add-ToolDirectory([string]$path) {
    if ((Test-Path $path) -and (($env:PATH -split ';') -notcontains $path)) {
        $env:PATH = "$path;$env:PATH"
    }
}

Add-ToolDirectory (Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin')
Add-ToolDirectory (Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja')

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'cmake was not found after entering the Visual Studio environment.'
}

if (-not $Quiet) {
    Write-Host "Visual Studio: $vsPath"
    Write-Host "CMake:         $((Get-Command cmake).Source)"
    $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if ($ninja) {
        Write-Host "Ninja:         $($ninja.Source)"
    } else {
        Write-Warning 'ninja was not found; CMake presets use the Ninja generator.'
    }
}
