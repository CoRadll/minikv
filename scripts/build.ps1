#Requires -Version 5.1
<#
.SYNOPSIS
    Configure and build MiniKV.

.DESCRIPTION
    Uses CMake presets when available, otherwise falls back to plain cmake.
    Every cmake invocation is checked; a non-zero exit code aborts the script.

    NOTE: this file is intentionally ASCII-only. Windows PowerShell 5.1 reads
    scripts without a BOM as ANSI, so non-ASCII text would be mis-decoded.

.PARAMETER Config
    Build configuration: Debug / Release / RelWithDebInfo.

.PARAMETER Clean
    Remove the build directory before configuring.

.PARAMETER Asan
    Enable AddressSanitizer and build into build-asan (requires the
    "C++ AddressSanitizer" component of Visual Studio).

.EXAMPLE
    .\scripts\build.ps1

.EXAMPLE
    .\scripts\build.ps1 -Config Release -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'Debug',

    [switch]$Clean,

    [switch]$Asan
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    $buildDir = if ($Asan) { 'build-asan' } else { 'build' }
    $buildPath = Join-Path $root $buildDir

    if ($Clean -and (Test-Path $buildPath)) {
        Write-Host "Removing $buildDir ..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $buildPath
    }

    $hasPresets = $false
    try {
        $null = cmake --list-presets 2>$null
        if ($LASTEXITCODE -eq 0) { $hasPresets = $true }
    } catch {
        $hasPresets = $false
    }

    $configureNeeded = -not (Test-Path (Join-Path $buildPath 'CMakeCache.txt'))

    if ($configureNeeded) {
        Write-Host "Configuring $buildDir ($Config) ..." -ForegroundColor Cyan
        if ($hasPresets -and -not $Asan) {
            $cmakeArgs = @('--preset', 'msvc')
            if ($Config -ne 'Debug') {
                $cmakeArgs += @('-DCMAKE_BUILD_TYPE=RelWithDebInfo')
            }
            & cmake @cmakeArgs
        } else {
            $genArgs = @('-S', '.', '-B', $buildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64')
            if ($Asan) { $genArgs += '-DMINIKV_ENABLE_ASAN=ON' }
            & cmake @genArgs
        }
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }
    } else {
        Write-Host "Reusing $buildDir (use -Clean to force reconfigure)" -ForegroundColor DarkGray
    }

    Write-Host "Building $buildDir / $Config ..." -ForegroundColor Cyan
    & cmake --build $buildDir --config $Config
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

    $exe = Join-Path $buildPath (Join-Path 'bin' $Config | Join-Path -ChildPath 'minikv.exe')
    Write-Host ""
    Write-Host "Build succeeded:" -ForegroundColor Green
    if (Test-Path $exe) { Write-Host "  $exe" }
} finally {
    Pop-Location
}
