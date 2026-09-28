param(
    [string]$EngineRoot='E:/UE/UE_5.8',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name='Current'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskLog=Join-Path $taskRoot "Saved/Logs/LiquidPerf_$Name.log"
$taskEditor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$taskArgs=@("`"$taskRoot/MessControl.uproject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-MCLiquidPerf',
    '-RenderOffscreen','-windowed','-ForceRes','-ResX=1920','-ResY=1080','-unattended','-nosound','-nosplash','-nop4',
    '-NoScreenMessages','-ExecCmds="t.MaxFPS 0,r.VSync 0,t.IdleWhenNotForeground 0"',"`"-abslog=$taskLog`"")
$taskProcess=Start-Process -FilePath $taskEditor -ArgumentList $taskArgs -WindowStyle Hidden -PassThru
try {
    if (-not $taskProcess.WaitForExit(100000)) {throw 'Liquid performance capture timed out.'}
    if ($taskProcess.ExitCode -ne 0) {throw "Liquid performance capture failed: $($taskProcess.ExitCode)"}
    Copy-Item -LiteralPath (Join-Path $taskRoot 'Saved/Profiling/LiquidAB.csv') -Destination (Join-Path $taskRoot "Saved/Profiling/LiquidAB_$Name.csv")
    Select-String -LiteralPath $taskLog -Pattern 'MC_LIQUID_PERF'
} finally {
    if (-not $taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id}
}
