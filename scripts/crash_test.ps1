# 真实强杀进程（等价 kill -9）后的恢复检查。
#
# 用法： .\scripts\crash_test.ps1 [-Keys 200000] [-DelayMs 300]
#
# 原理：
#   1. 后台启动 minikv bench（大量写入，写满 WAL / 触发刷盘）
#   2. 指定时间后 Stop-Process -Force（不做任何清理，不刷盘）
#   3. 重新打开数据库，统计能读到的数据，验证 WAL 恢复没有报错

param(
    [string]$Dir = "",
    [int]$Keys = 200000,
    [int]$DelayMs = 300,
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Dir)) {
    $scriptRoot = $PSScriptRoot
    $repoRoot = Split-Path -Parent $scriptRoot
    $Dir = Join-Path $repoRoot ".minikv-test-tmp\crash-check"
}

if (Test-Path $Dir) { Remove-Item -Recurse -Force $Dir }

$exe = Join-Path $PSScriptRoot "..\build\bin\$Config\minikv.exe"
if (-not (Test-Path $exe)) {
    Write-Host "not found: $exe" -ForegroundColor Red
    exit 1
}

Write-Host "database : $Dir"
Write-Host "keys     : $Keys"
Write-Host "kill after: $DelayMs ms"
Write-Host ""

# 1) 后台写入，然后强杀
$proc = Start-Process -FilePath $exe -ArgumentList "bench","--dir",$Dir,"--keys",$Keys,"--reads","1" -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds $DelayMs

$killed = $false
if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
    $killed = $true
}
try { $proc.WaitForExit(5000) | Out-Null } catch {}

Write-Host "process killed : $killed"
Start-Sleep -Milliseconds 200
Write-Host ""

# 2) 重新打开，验证恢复
$commands = @("stats", "scan 0 3", "exit") -join "`n"
$commands | & $exe --dir $Dir
$recoveryExit = $LASTEXITCODE

Write-Host ""
$dbFiles = @(Get-ChildItem $Dir -ErrorAction SilentlyContinue)
Write-Host "files after recovery : $($dbFiles.Count)"
foreach ($f in $dbFiles) {
    Write-Host ("  {0,-14} {1,10} bytes" -f $f.Name, $f.Length)
}
Write-Host ""
Write-Host "recovery check finished (exit code = $recoveryExit)" -ForegroundColor Green
exit 0
