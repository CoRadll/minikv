#Requires -Version 5.1
<#
.SYNOPSIS
    Verify the one-to-one layout between include/ and src/.

.DESCRIPTION
    Rules (see docs/decisions.md, D-003):
      1. Every src/**/*.cc (except src/tools) has a same-path .h under include/minikv/
      2. Every include/**/*.h has a same-path .cc under src/, unless whitelisted
      3. Every .cc includes its own header (header self-containment)
      4. Every whitelisted file actually exists (no stale exceptions)

    Exits with code 1 on any violation.

    NOTE: ASCII-only on purpose - Windows PowerShell 5.1 reads scripts without
    a BOM as ANSI and would mis-decode non-ASCII text.

.PARAMETER Root
    Project root, defaults to the parent of this script.
#>
[CmdletBinding()]
param(
    [string]$Root = ''
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot is not reliably populated while param() defaults are evaluated,
# so resolve the default inside the body instead.
if ([string]::IsNullOrEmpty($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

# Header-only files: pure definitions or pure interfaces.
# Any new entry must also be documented in docs/decisions.md (D-003).
$exceptions = @(
    'api/options.h',
    'api/iterator.h',
    'api/version.h'
)

function Get-RelPath([string]$BaseDir, [string]$FullPath) {
    $sep = [System.IO.Path]::DirectorySeparatorChar
    $base = $BaseDir.TrimEnd('\', '/')
    if ($FullPath.StartsWith($base + $sep, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $FullPath.Substring($base.Length + 1).Replace('\', '/')
    }
    return $FullPath.Replace('\', '/')
}

$includeRoot = Join-Path $Root 'include/minikv'
$srcRoot = Join-Path $Root 'src'

if (-not (Test-Path $includeRoot)) { throw "Not found: $includeRoot" }
if (-not (Test-Path $srcRoot)) { throw "Not found: $srcRoot" }

$violations = New-Object 'System.Collections.Generic.List[string]'

Write-Host "Layout check: $Root" -ForegroundColor Cyan

# 1. every .cc needs a matching .h (src/tools holds entry points, out of scope)
$ccFiles = @(Get-ChildItem -Path $srcRoot -Recurse -Filter *.cc |
    Where-Object { (Get-RelPath $srcRoot $_.FullName) -notlike 'tools/*' })

foreach ($cc in $ccFiles) {
    $rel = Get-RelPath $srcRoot $cc.FullName
    $headerRel = $rel -replace '\.cc$', '.h'
    $headerPath = Join-Path $includeRoot ($headerRel -replace '/', '\')
    if (-not (Test-Path $headerPath)) {
        $violations.Add("missing header: src/$rel  ->  include/minikv/$headerRel")
    }
}

# 2. every .h needs a matching .cc unless whitelisted
$hFiles = @(Get-ChildItem -Path $includeRoot -Recurse -Filter *.h)
foreach ($h in $hFiles) {
    $rel = Get-RelPath $includeRoot $h.FullName
    if ($exceptions -contains $rel) { continue }

    $ccRel = $rel -replace '\.h$', '.cc'
    if (-not (Test-Path (Join-Path $srcRoot ($ccRel -replace '/', '\')))) {
        $violations.Add("missing implementation: include/minikv/$rel  ->  src/$ccRel")
    }
}

# 3. every .cc must include its own header
foreach ($cc in $ccFiles) {
    $rel = Get-RelPath $srcRoot $cc.FullName
    $headerRel = $rel -replace '\.cc$', '.h'
    $expected = "#include `"minikv/$headerRel`""
    $content = Get-Content -Path $cc.FullName -Raw
    if ($content.IndexOf($expected, [System.StringComparison]::Ordinal) -lt 0) {
        $violations.Add("does not include its own header: src/$rel (expected $expected)")
    }
}

# 4. whitelist entries must exist
foreach ($name in $exceptions) {
    $probe = Join-Path $includeRoot ($name -replace '/', '\')
    if (-not (Test-Path $probe)) {
        $violations.Add("stale whitelist entry: include/minikv/$name does not exist")
    }
}

# report
Write-Host ("  implementations: {0}   headers: {1}   whitelisted: {2}" -f $ccFiles.Count, $hFiles.Count, $exceptions.Count)

if ($violations.Count -eq 0) {
    Write-Host "  Layout OK." -ForegroundColor Green
    exit 0
}

Write-Host ("  {0} problem(s) found:" -f $violations.Count) -ForegroundColor Red
foreach ($v in $violations) { Write-Host "    - $v" -ForegroundColor Red }
exit 1
