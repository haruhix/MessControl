param([string]$EngineRoot = $env:UE_ROOT, [switch]$Game, [string[]]$AdditionalArguments=@())
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
$taskBuildLogs = Join-Path $taskProjectRoot 'Saved\Build'
$taskBuildTemp = Join-Path $taskBuildLogs 'Temp'
$taskBuildCache = Join-Path $taskBuildLogs 'UBA'
New-Item -ItemType Directory -Force -Path $taskBuildTemp, $taskBuildLogs | Out-Null
$taskPreviousTemp = $env:TEMP
$taskPreviousTmp = $env:TMP
try {
    $env:TEMP = $taskBuildTemp
    $env:TMP = $taskBuildTemp
    $taskBuildArguments = @($AdditionalArguments)
    if (-not ($taskBuildArguments | Where-Object { $_ -like '-log=*' })) {
        $taskBuildArguments += "-log=$taskBuildLogs\$taskTarget.log"
    }
    if (-not ($taskBuildArguments | Where-Object { $_ -like '-UBARootDir=*' })) {
        $taskBuildArguments += "-UBARootDir=$taskBuildCache"
    }
    & "$EngineRoot\Engine\Build\BatchFiles\Build.bat" $taskTarget Win64 Development "-Project=$taskProjectRoot\MessControl.uproject" -WaitMutex -NoHotReloadFromIDE @taskBuildArguments
    if ($LASTEXITCODE -ne 0) { throw "Unreal build failed: $LASTEXITCODE" }
}
finally {
    $env:TEMP = $taskPreviousTemp
    $env:TMP = $taskPreviousTmp
}
