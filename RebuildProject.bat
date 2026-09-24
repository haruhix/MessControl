@echo off
setlocal EnableExtensions
chcp 65001 >nul

rem Close Unreal Editor before running. An optional first argument is the engine root.
rem Set MESSCONTROL_NO_PAUSE=1 for unattended use. Pass -DryRun to preview actions.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Rebuild.ps1" %*
set "REBUILD_EXIT_CODE=%ERRORLEVEL%"

echo.
if not defined MESSCONTROL_NO_PAUSE pause
exit /b %REBUILD_EXIT_CODE%
