param([string]$EngineRoot='E:\UE\UE_5.8',[switch]$Capture)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskLog=Join-Path $taskRoot 'Saved\Logs\UlcerReworkSmoke.log'
New-Item -ItemType Directory -Path (Join-Path $taskRoot 'Artifacts\UlcerRework') -Force | Out-Null
$taskArgs=@("`"$taskRoot\MessControl.uproject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-MCLegacyDays','-MCUlcerReworkTest','-nullrhi','-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"",'"-ExecCmds=t.MaxFPS 30"','-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=30')
if($Capture) {
    $taskArgs=@($taskArgs | Where-Object {$_ -ne '-nullrhi' -and $_ -notlike '*ExecCmds=*'})
    $taskArgs+=@('-MCUlcerCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','"-ExecCmds=t.MaxFPS 15,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,r.Streaming.PoolSize 256,r.ScreenPercentage 85"')
}
$taskProcess=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') -ArgumentList $taskArgs -WindowStyle Hidden -PassThru
try {
    if(-not $taskProcess.WaitForExit(180000)){throw 'Ulcer rework smoke timed out.'}
    if($taskProcess.ExitCode -ne 0 -or -not (Select-String -LiteralPath $taskLog -Pattern 'MC_ULCER_PASS' -Quiet)){throw "Ulcer rework failed: $taskLog"}
    Select-String -LiteralPath $taskLog -Pattern 'MC_ULCER_(CHECK|PASS)'
} finally {
    if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}
}
