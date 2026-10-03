param([string]$EngineRoot=$env:UE_ROOT,[switch]$AuditOnly,[switch]$VerifyIdempotence)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw 'C:/ProgramData/Epic/UnrealEngineLauncher/LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
$taskEditor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if(-not(Test-Path -LiteralPath $taskEditor)){throw 'UE 5.8 editor was not found.'}
$taskReport=Join-Path $taskRoot 'Saved/FoodCollisionBake/MenuCollision.json'
$taskLog=Join-Path $taskRoot 'Saved/Logs/MenuFoodCollisionBake.log'
New-Item -ItemType Directory -Path (Split-Path -Parent $taskReport),(Split-Path -Parent $taskLog) -Force | Out-Null
if(Test-Path -LiteralPath $taskReport){Remove-Item -LiteralPath $taskReport}
$taskPython="$taskRoot/Tools/Unreal/bake_menu_food_collision.py"
if($AuditOnly){$taskPython+=' --audit-only'}
if($VerifyIdempotence){$taskPython+=' --verify-idempotence'}
$taskPreviousSdkSetup=$env:UE_SKIP_UBT_SDK_SETUP
try {
    $env:UE_SKIP_UBT_SDK_SETUP='1'
    & $taskEditor "$taskRoot/MessControl.uproject" -nullrhi -unattended -nosplash -nop4 -MCFoodCollisionNoAutoBake "-ExecutePythonScript=$taskPython" "-abslog=$taskLog"
    if($LASTEXITCODE -ne 0){throw "Food collision authoring failed ($LASTEXITCODE): $taskLog"}
} finally {
    $env:UE_SKIP_UBT_SDK_SETUP=$taskPreviousSdkSetup
}
if(-not(Test-Path -LiteralPath $taskReport)){throw "Food collision bake report missing: $taskLog"}
$taskResult=Get-Content -Raw -LiteralPath $taskReport | ConvertFrom-Json
if(-not $taskResult.complete -or $taskResult.errors.Count -ne 0){throw "Food collision bake did not complete: $taskReport"}
Write-Output "Food collision prepared: $taskReport"
