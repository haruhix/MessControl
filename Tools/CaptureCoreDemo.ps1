param([string]$EngineRoot=$env:UE_ROOT,[string]$FFmpeg='C:\ffmpeg\ffmpeg.exe')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskFrames=Join-Path $taskRoot 'Saved\CoreDemoFrames'
New-Item -ItemType Directory -Path $taskFrames -Force | Out-Null
Get-ChildItem -LiteralPath $taskFrames -Filter 'Frame*.png' -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
& "$PSScriptRoot\Test.ps1" -EngineRoot $EngineRoot -Mode CoreNetwork -CaptureCore
# Test.ps1 throws on failure; Start-Process does not set the caller's LASTEXITCODE.
New-Item -ItemType Directory -Path "$taskRoot\Artifacts" -Force | Out-Null
& $FFmpeg -hide_banner -loglevel warning -y -f concat -safe 0 -i "$taskFrames\Timing.txt" -vf 'fps=30' -c:v libx264 -crf 19 -pix_fmt yuv420p -movflags +faststart "$taskRoot\Artifacts\Step03_06_Gameplay.mp4"
if ($LASTEXITCODE -ne 0) { throw 'Core video encoding failed.' }
& $FFmpeg -hide_banner -loglevel warning -y -ss 1 -i "$taskRoot\Artifacts\Step03_06_Gameplay.mp4" -frames:v 1 -update 1 "$taskRoot\Artifacts\Step03_06_Gameplay.png"
if ($LASTEXITCODE -ne 0) { throw 'Core cover extraction failed.' }
Write-Output "$taskRoot\Artifacts\Step03_06_Gameplay.mp4"
