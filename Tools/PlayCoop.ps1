param([string]$EngineRoot = $env:UE_ROOT, [ValidateRange(1,4)][int]$Players=2)
$ErrorActionPreference='Stop'
$taskProjectRoot=Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$taskProject=Join-Path $taskProjectRoot 'MessControl.uproject'
# These are intentionally visible interactive game windows requested by this launcher.
Start-Process -FilePath $taskEditor -ArgumentList @("`"$taskProject`"",'/Game/Maps/L_Mouth?listen','-game','-windowed','-ResX=960','-ResY=600','-WinX=0','-WinY=30','-log','-nosplash')
for ($taskIndex=1;$taskIndex -lt $Players;$taskIndex++) {
    Start-Process -FilePath $taskEditor -ArgumentList @("`"$taskProject`"",'127.0.0.1:7777','-game','-windowed','-ResX=960','-ResY=600',"-WinX=$($taskIndex*160)","-WinY=$($taskIndex*100+30)",'-log','-nosplash')
}
