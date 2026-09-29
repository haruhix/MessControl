param([string]$EngineRoot='E:\UE\UE_5.8',[switch]$EncodeOnly)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $EncodeOnly){& "$PSScriptRoot\TestBrush.ps1" -EngineRoot $EngineRoot -Solo -Capture}
$taskFfmpeg=(Get-Command ffmpeg -ErrorAction Stop).Source
$taskTiming=Join-Path $taskRoot 'Saved\BrushFrames\times.csv'
if(-not (Test-Path -LiteralPath $taskTiming)){throw 'No brush recording. Run the capture first.'}
& $taskFfmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i $taskTiming -vf fps=30 -c:v libx264 -crf 19 -pix_fmt yuv420p -movflags +faststart (Join-Path $taskRoot 'Artifacts\BrushCleaning.mp4')
if($LASTEXITCODE -ne 0){throw 'Video encoding failed.'}
Write-Output (Join-Path $taskRoot 'Artifacts\BrushCleaning.mp4')
