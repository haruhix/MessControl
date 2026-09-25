param([string]$EngineRoot=$env:UE_ROOT,[string]$FFmpeg='C:\ffmpeg\ffmpeg.exe')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskFrames=Join-Path $taskRoot 'Saved\DayOneFrames'
New-Item -ItemType Directory -Path $taskFrames -Force | Out-Null
Get-ChildItem -LiteralPath $taskFrames -Filter 'Frame*.png' -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
& "$PSScriptRoot\Test.ps1" -EngineRoot $EngineRoot -Mode DayOneNetwork -CaptureDayOne
New-Item -ItemType Directory -Path "$taskRoot\Artifacts" -Force | Out-Null
& $FFmpeg -hide_banner -loglevel warning -y -f concat -safe 0 -i "$taskFrames\Timing.txt" -vf 'fps=30' -c:v libx264 -crf 20 -pix_fmt yuv420p -movflags +faststart "$taskRoot\Artifacts\Day01_Mechanics.mp4"
if ($LASTEXITCODE -ne 0) { throw 'Day one video encoding failed.' }
& $FFmpeg -hide_banner -loglevel warning -y -ss 3 -i "$taskRoot\Artifacts\Day01_Mechanics.mp4" -frames:v 1 -update 1 "$taskRoot\Artifacts\Day01_Mechanics.png"
if ($LASTEXITCODE -ne 0) { throw 'Day one cover extraction failed.' }
Write-Output "$taskRoot\Artifacts\Day01_Mechanics.mp4"
