@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Stop.ps1" %*
set "audioDeskExit=%errorlevel%"
if not "%audioDeskExit%"=="0" pause
exit /b %audioDeskExit%
