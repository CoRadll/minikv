#Requires -Version 5.1
<#
.SYNOPSIS
    Run MiniKV tests.

.DESCRIPTION
    Runs the include/src layout check first, then ctest. Any failure ends the
    script with a non-zero exit code.

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads scripts without
    a BOM as ANSI and would mis-decode non-ASCII text.

.PARAMETER Config
    Build configuration, must match the one used for building.

.PARAMETER Build
    Build before testing.

.PARAMETER SkipLayout
    Skip the layout check.

.PARAMETER Filter
    ctest -R regex to select tests.

.EXAMPLE
    .\scripts\test.ps1 -Build

.EXAMPLE
    .\scripts\test.ps1 -Filter wal
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'Debug',

    [switch]$Build,

    [switch]$SkipLayout,

    [string]$Filter = ''
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildScript = Join-Path $PSScriptRoot 'build.ps1'
$layoutScript = Join-Path $PSScriptRoot 'check_layout.ps1'
$self = (Get-Process -Id $PID).Path

Push-Location $root
try {
    if ($Build) {
        & $self -NoProfile -ExecutionPolicy Bypass -File $buildScript -Config $Config
        if ($LASTEXITCODE -ne 0) { throw 'Build failed, aborting tests' }
    }

    if (-not $SkipLayout) {
        & $self -NoProfile -ExecutionPolicy Bypass -File $layoutScript
        if ($LASTEXITCODE -ne 0) { throw 'Layout check failed, aborting tests' }
    }

    $buildDir = Join-Path $root 'build'
    if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        throw "No build directory at $buildDir - run scripts\build.ps1 first"
    }

    Write-Host ""
    Write-Host "Running tests ($Config) ..." -ForegroundColor Cyan
    $ctestArgs = @('--test-dir', $buildDir, '-C', $Config, '--output-on-failure')
    if ($Filter) { $ctestArgs += @('-R', $Filter) }

    & ctest @ctestArgs
    if ($LASTEXITCODE -ne 0) { throw "Tests failed (exit $LASTEXITCODE)" }

    Write-Host ""
    Write-Host "All tests passed." -ForegroundColor Green
} finally {
    Pop-Location
}
