param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$clangFormat = (Get-Command clang-format -ErrorAction SilentlyContinue).Source

if (-not $clangFormat) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        # Prefer VS 2022, matching the pinned toolchain and the CI image.
        $vsPath = & $vswhere -latest -version '[17.0,18.0)' -products * `
            -property installationPath
        if (-not $vsPath) {
            $vsPath = & $vswhere -latest -products * -property installationPath
        }
        $candidates = @(
            (Join-Path $vsPath 'VC\Tools\Llvm\x64\bin\clang-format.exe'),
            (Join-Path $vsPath 'VC\Tools\Llvm\bin\clang-format.exe'),
            (Join-Path $vsPath 'VC\Tools\Llvm\ARM64\bin\clang-format.exe')
        )
        foreach ($candidate in $candidates) {
            if (Test-Path $candidate) {
                $clangFormat = $candidate
                break
            }
        }
    }
}

if (-not $clangFormat -and (Test-Path 'C:\Program Files\LLVM\bin\clang-format.exe')) {
    $clangFormat = 'C:\Program Files\LLVM\bin\clang-format.exe'
}

if (-not $clangFormat) {
    throw 'clang-format not found. Install the LLVM tools component in Visual Studio or put clang-format on PATH.'
}

$files = Get-ChildItem -Path $root -Recurse -File |
    Where-Object {
        $_.Extension -in '.h', '.hpp', '.cpp', '.cc', '.cxx', '.inl' -and
        $_.FullName -notmatch '\\build\\' -and
        $_.FullName -notmatch '\\third_party\\'
    }

if (-not $files) {
    Write-Host 'No C++ files to format.'
    exit 0
}

if ($Check) {
    & $clangFormat --dry-run --Werror --style=file $files.FullName
} else {
    & $clangFormat -i --style=file $files.FullName
}

exit $LASTEXITCODE
