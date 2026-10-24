@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start.ps1" %*
set "audioDeskExit=%errorlevel%"
if not "%audioDeskExit%"=="0" (
  echo.
  echo Audio Desk could not start. Read the error above.
  pause
)
exit /b %audioDeskExit%
