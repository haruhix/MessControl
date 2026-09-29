param([string]$EngineRoot='E:\UE\UE_5.8',[ValidateSet(30,60,120)][int]$FrameRate=60)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskLog=Join-Path $taskRoot "Saved\Logs\Inventory$FrameRate.log"
$taskArgs=@("`"$taskRoot\MessControl.uproject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-MCLegacyDays','-MCInventoryTest','-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"",'-UseFixedTimeStep',"-FPS=$FrameRate",'-RenderOffscreen','-windowed','-ForceRes','-ResX=1536','-ResY=864','-NoScreenMessages','"-ExecCmds=t.MaxFPS 60,t.IdleWhenNotForeground 0,r.ScreenPercentage 100"')
$taskProcess=Start-Process (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
try {
    if(-not $taskProcess.WaitForExit(210000)){throw 'Inventory validation timed out.'}
    $taskResults=Select-String -LiteralPath $taskLog -Pattern 'MC_INVENTORY_(CHECK|PASS|FAIL)'
    $taskResults.Line | Set-Content -LiteralPath (Join-Path $taskRoot 'Artifacts\Inventory_Validation.txt') -Encoding utf8
    $taskResults.Line
    if($taskProcess.ExitCode -ne 0 -or -not ($taskResults.Line -match 'MC_INVENTORY_PASS')){throw 'Inventory validation failed.'}
} finally { if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id} }
