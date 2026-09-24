param([string]$EngineRoot=$env:UE_ROOT,[string]$FFmpeg='C:\ffmpeg\ffmpeg.exe')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
if (-not (Test-Path -LiteralPath $FFmpeg)) { throw 'Pass -FFmpeg with the path to ffmpeg.exe.' }
$taskFrames=Join-Path $taskRoot 'Saved\ArenaDemoFrames'
New-Item -ItemType Directory -Path $taskFrames -Force | Out-Null
# Only previously generated frame files in this exact workspace directory are replaced.
Get-ChildItem -LiteralPath $taskFrames -Filter 'Frame*.png' -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
$taskLog=Join-Path $taskRoot 'Saved\Logs\ArenaDemo.log'
New-Item -ItemType Directory -Path (Split-Path $taskLog) -Force | Out-Null
if (Test-Path -LiteralPath $taskLog) { Remove-Item -LiteralPath $taskLog }
$taskArgs=@("`"$taskRoot\MessControl.uproject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-MCArenaDemo','-UseFixedTimeStep','-FPS=30','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"")
$taskProcess=Start-Process -FilePath "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -WindowStyle Hidden -ArgumentList $taskArgs -PassThru
try {
    if (-not $taskProcess.WaitForExit(1200000)) { throw 'Arena recording timed out.' }
    if ($taskProcess.ExitCode -ne 0 -or -not (Select-String -Path $taskLog -Pattern 'MC_ARENA_DEMO_PASS' -Quiet)) { throw 'Arena recording failed. See ArenaDemo.log.' }
    if (Select-String -Path $taskLog -Pattern 'Failed to compile Material|LogMaterial: Error|LogShaderCompilers: Error' -Quiet) { throw 'A material failed to compile; inspect ArenaDemo.log before recording again.' }
} finally { if (-not $taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id } }
$taskCount=(Get-ChildItem -LiteralPath $taskFrames -Filter 'Frame*.png' -File).Count
if ($taskCount -lt 688 -or $taskCount -gt 692) { throw "Unexpected frame count: $taskCount" }
New-Item -ItemType Directory -Path "$taskRoot\Artifacts" -Force | Out-Null
& $FFmpeg -hide_banner -loglevel warning -y -framerate 30 -i "$taskFrames\Frame%05d.png" -c:v libx264 -crf 18 -preset medium -pix_fmt yuv420p -movflags +faststart "$taskRoot\Artifacts\Step02_ArenaTeeth.mp4"
if ($LASTEXITCODE -ne 0) { throw 'Video encoding failed.' }
& $FFmpeg -hide_banner -loglevel warning -y -ss 14 -i "$taskRoot\Artifacts\Step02_ArenaTeeth.mp4" -frames:v 1 -update 1 "$taskRoot\Artifacts\Step02_ArenaTeeth.png"
if ($LASTEXITCODE -ne 0) { throw 'Video cover extraction failed.' }
Write-Output "Recorded $taskCount frames: $taskRoot\Artifacts\Step02_ArenaTeeth.mp4"
