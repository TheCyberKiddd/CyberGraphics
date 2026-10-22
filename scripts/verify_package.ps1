param(
    [string]$Preset = 'debug',
    [string]$Prefix = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

. (Join-Path $PSScriptRoot 'bootstrap.ps1') -Quiet

if ($Preset -eq 'debug-asan') {
    throw 'ASan runtime is build-local; verify a debug or release package instead.'
}

$buildDir = Join-Path $root "build/$Preset"
if (-not (Test-Path $buildDir)) {
    throw "Build directory not found: $buildDir. Build the preset first."
}

if (-not $Prefix) {
    $Prefix = Join-Path $root "build/install-$Preset"
    if (Test-Path $Prefix) {
        Remove-Item -Recurse -Force $Prefix
    }
}
$consumerDir = Join-Path $root "build/package-consumer-$Preset"
if (Test-Path $consumerDir) {
    Remove-Item -Recurse -Force $consumerDir
}

$buildType = if ($Preset -eq 'release' -or $Preset -eq 'shipping') { 'Release' } else { 'Debug' }

& cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --install $buildDir --prefix $Prefix
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$requiredHeaders = @(
    'include/cyber/core/version_generated.h',
    'include/cyber/math/math.h',
    'include/cyber/math/mat4.h',
    'include/cyber/math/types.h',
    'include/cyber/math/vec3.h',
    'include/cyber/memory/memory.h',
    'include/cyber/memory/allocator.h',
    'include/cyber/memory/dynamic_array.h',
    'include/cyber/memory/string_view.h',
    'include/cyber/platform/platform.h',
    'include/cyber/platform/window.h',
    'include/cyber/platform/thread.h',
    'include/cyber/perf/perf.h',
    'include/cyber/perf/profiler.h',
    'include/cyber/jobs/jobs.h',
    'include/cyber/jobs/job_system.h'
)
foreach ($header in $requiredHeaders) {
    $path = Join-Path $Prefix $header
    if (-not (Test-Path $path)) {
        throw "Installed package is missing $header"
    }
}

& cmake -S tests/integration/package_consumer -B $consumerDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$buildType" "-DCMAKE_PREFIX_PATH=$Prefix"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build $consumerDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$executable = if ($env:OS -eq 'Windows_NT') { 'consumer.exe' } else { 'consumer' }
& (Join-Path $consumerDir $executable)
exit $LASTEXITCODE
