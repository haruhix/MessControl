param([string]$EngineRoot=$env:UE_ROOT,[switch]$Capture)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw -LiteralPath 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProject=Join-Path $taskRoot 'MessControl.uproject'
$taskLog=Join-Path $taskRoot 'Saved\Logs\FoodZonesSmoke.log'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $taskLog) | Out-Null
if(Test-Path -LiteralPath $taskLog) {Remove-Item -LiteralPath $taskLog}
$taskArgs=@("`"$taskProject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-nosteam','-MCFoodZonesSmoke','-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"")
if($Capture) {
    $taskArgs+=@('-MCFoodZonesCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','-ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,sg.PostProcessQuality 1,r.ScreenPercentage 100"')
} else {
    $taskArgs+=@('-nullrhi','-ExecCmds="t.MaxFPS 60"')
}
$taskProcess=$null
try {
    $taskProcess=Start-Process -FilePath $taskEditor -WindowStyle Hidden -PassThru -ArgumentList $taskArgs
    if(-not $taskProcess.WaitForExit(130000)) {throw "Food zone smoke timed out. See $taskLog"}
    if($taskProcess.ExitCode -ne 0 -or -not (Select-String -LiteralPath $taskLog -Pattern 'MC_FOOD_ZONES_PASS' -Quiet) -or (Select-String -LiteralPath $taskLog -Pattern 'MC_FOOD_ZONES_FAIL' -Quiet)) {
        throw "Food zone smoke failed. See $taskLog"
    }
    Write-Output 'PASS: six food-zone interactions using real collection, walking, Q, rigid-body grip, timed intake and permanent inventory-brush protection.'
} finally {
    if($taskProcess -and -not $taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id}
}
