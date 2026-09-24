param([string]$EngineRoot = $env:UE_ROOT, [switch]$Game)
$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) {
    $taskManifest = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if (Test-Path -LiteralPath $taskManifest) {
        $taskInstalls = (Get-Content -Raw -LiteralPath $taskManifest | ConvertFrom-Json).InstallationList
        $EngineRoot = ($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
    }
}
if (-not $EngineRoot -or -not (Test-Path -LiteralPath "$EngineRoot\Engine\Build\BatchFiles\Build.bat")) { throw 'UE 5.8 not found. Set UE_ROOT or pass -EngineRoot.' }
$taskTarget = if ($Game) { 'MessControl' } else { 'MessControlEditor' }
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" $taskTarget Win64 Development "-Project=$taskProjectRoot\MessControl.uproject" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw "Unreal build failed: $LASTEXITCODE" }
