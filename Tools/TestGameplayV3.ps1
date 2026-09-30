param([string]$EngineRoot='E:\UE\UE_5.8',[switch]$Capture)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskLog=Join-Path $taskRoot 'Saved\Logs\GameplayV3Smoke.log'
New-Item -ItemType Directory -Path (Join-Path $taskRoot 'Artifacts\GameplayV3') -Force | Out-Null
$taskArgs=@("`"$taskRoot\MessControl.uproject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-MCLegacyDays','-MCGameplayV3Test','-nullrhi','-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"",'"-ExecCmds=t.MaxFPS 30"','-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=30')
if($Capture) {
    $taskArgs=@($taskArgs | Where-Object {$_ -ne '-nullrhi' -and $_ -notlike '*ExecCmds=*'})
    $taskArgs+=@('-MCGameplayV3Capture','-RenderOffscreen','-windowed','-ForceRes','-ResX=900','-ResY=600','-NoScreenMessages','"-ExecCmds=t.MaxFPS 15,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,r.Streaming.PoolSize 256,r.ScreenPercentage 80"')
}
$taskProcess=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') -ArgumentList $taskArgs -WindowStyle Hidden -PassThru
try {
    if(-not $taskProcess.WaitForExit(300000)){throw 'Gameplay V3 smoke timed out.'}
    $taskResult=Select-String -LiteralPath $taskLog -Pattern 'MC_VALIDATION_(PASS|FAIL) GAMEPLAY_V3' | Select-Object -Last 1
    if($taskProcess.ExitCode -ne 0 -or -not $taskResult -or $taskResult.Line -notmatch 'MC_VALIDATION_PASS'){throw "Gameplay V3 failed: $taskLog"}
    $taskResult.Line | Set-Content -LiteralPath (Join-Path $taskRoot 'Artifacts\GameplayV3\Validation.txt') -Encoding utf8
    $taskResult.Line
} finally {
    if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}
}
