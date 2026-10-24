@echo off
REM Wrapper: .cmd files are not subject to PowerShell ExecutionPolicy.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test.ps1" %*
exit /b %ERRORLEVEL%
