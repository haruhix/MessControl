param([string]$EngineRoot=$env:UE_ROOT,[string]$FFmpeg='C:\ffmpeg\ffmpeg.exe')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskFrames=Join-Path $taskRoot 'Saved\CoffeeWaterFrames'
New-Item -ItemType Directory -Path $taskFrames -Force | Out-Null
Get-ChildItem -LiteralPath $taskFrames -Filter 'Frame*.png' -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
& "$PSScriptRoot\Test.ps1" -EngineRoot $EngineRoot -Mode CoffeeNetwork -PacketLagMs 75 -PacketLoss 2 -CaptureCoffee
& $FFmpeg -hide_banner -loglevel warning -y -f concat -safe 0 -i "$taskFrames\Timing.txt" -vf 'fps=30' -c:v libx264 -crf 20 -pix_fmt yuv420p -movflags +faststart "$taskRoot\Artifacts\CoffeeWater.mp4"
if ($LASTEXITCODE -ne 0) { throw 'Coffee recording encoding failed.' }
& $FFmpeg -hide_banner -loglevel warning -y -ss 2.5 -i "$taskRoot\Artifacts\CoffeeWater.mp4" -frames:v 1 -update 1 "$taskRoot\Artifacts\CoffeeWater.png"
if ($LASTEXITCODE -ne 0) { throw 'Coffee cover failed.' }
