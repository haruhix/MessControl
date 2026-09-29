param([string]$EngineRoot='E:\UE\UE_5.8',[switch]$Capture)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskLog=Join-Path $taskRoot 'Saved\Logs\BrushFacing.log'
$taskArgs=@("`"$taskRoot\MessControl.uproject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-MCLegacyDays','-MCBrushTest','-MCBrushFacing','-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"",'-UseFixedTimeStep','-FPS=30')
if($Capture){$taskArgs+=@('-MCBrushCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=850','-NoScreenMessages','"-ExecCmds=t.MaxFPS 60,t.IdleWhenNotForeground 0,r.ScreenPercentage 100"')}
else{$taskArgs+='-nullrhi'}
$taskProcess=Start-Process (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
try {
    if(-not $taskProcess.WaitForExit(210000)){throw 'Brush facing validation timed out.'}
    $taskResults=Select-String -LiteralPath $taskLog -Pattern 'MC_BRUSH_FACING_(CASE|PASS|FAIL)'
    $taskResults.Line | Set-Content -LiteralPath (Join-Path $taskRoot 'Artifacts\BrushFacing_Validation.txt') -Encoding utf8
    $taskResults.Line
    if(-not ($taskResults.Line -match 'MC_BRUSH_FACING_PASS')){throw 'Brush facing validation failed.'}
} finally {if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
