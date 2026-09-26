param([string]$EngineRoot=$env:UE_ROOT,[string]$FFmpeg='C:\ffmpeg\ffmpeg.exe',[switch]$ReuseFrames,[switch]$Jolt,[switch]$Pressure)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if ($Jolt -and $Pressure) { throw 'Choose Jolt or Pressure, not both.' }
$taskName=if($Pressure){'TonguePressure'}elseif($Jolt){'TongueJolt'}else{'TonguePain'}
$taskFolder=if($Pressure){'TonguePressureFrames'}elseif($Jolt){'TongueJoltFrames'}else{'TongueFrames'}
$taskMode=if($Pressure){'TonguePressureNetwork'}elseif($Jolt){'TongueJoltNetwork'}else{'TongueNetwork'}
$taskFrames=Join-Path $taskRoot ('Saved\'+$taskFolder)
if (-not $ReuseFrames) {
    New-Item -ItemType Directory -Path $taskFrames -Force | Out-Null
    Get-ChildItem -LiteralPath $taskFrames -Filter 'Tongue_*.png' -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
    & "$PSScriptRoot\Test.ps1" -EngineRoot $EngineRoot -Mode $taskMode -PacketLagMs 75 -PacketLoss 2 -CaptureTongue:(!$Pressure) -CapturePressure:$Pressure
}
$taskCulture=[Globalization.CultureInfo]::InvariantCulture
$taskRows=@(Get-Content -LiteralPath (Join-Path $taskFrames 'times.csv') | Where-Object { $_ } | ForEach-Object {
    $taskParts=$_ -split ','
    [PSCustomObject]@{ Name=$taskParts[0]; Time=[double]::Parse($taskParts[1],$taskCulture) }
})
if ($taskRows.Count -lt 2) { throw 'No timed tongue capture to encode.' }
$taskLines=[Collections.Generic.List[string]]::new()
for ($taskI=0;$taskI -lt $taskRows.Count;$taskI++) {
    if (-not (Test-Path -LiteralPath (Join-Path $taskFrames $taskRows[$taskI].Name))) { throw 'A captured frame is missing.' }
    $taskDuration=if($taskI+1 -lt $taskRows.Count){ $taskRows[$taskI+1].Time-$taskRows[$taskI].Time }else{ .1 }
    $taskLines.Add("file '$($taskRows[$taskI].Name)'")
    $taskLines.Add('duration '+$taskDuration.ToString('F6',$taskCulture))
}
$taskLines.Add("file '$($taskRows[-1].Name)'")
[IO.File]::WriteAllLines((Join-Path $taskFrames 'timing.txt'),$taskLines)
& $FFmpeg -hide_banner -loglevel warning -y -f concat -safe 0 -i "$taskFrames\timing.txt" -vf 'fps=30' -c:v libx264 -crf 20 -pix_fmt yuv420p -movflags +faststart "$taskRoot\Artifacts\$taskName.mp4"
if ($LASTEXITCODE -ne 0) { throw 'Tongue recording encoding failed.' }
$taskCoverTime=if($Pressure){'5'}elseif($Jolt){'3.35'}else{'2.7'}
& $FFmpeg -hide_banner -loglevel warning -y -ss $taskCoverTime -i "$taskRoot\Artifacts\$taskName.mp4" -frames:v 1 -update 1 "$taskRoot\Artifacts\$taskName.png"
if ($LASTEXITCODE -ne 0) { throw 'Tongue cover failed.' }
