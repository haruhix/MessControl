param([string]$EngineRoot=$env:UE_ROOT,[string]$FFmpeg='C:\ffmpeg\ffmpeg.exe',[switch]$ReuseFrames)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskName='Grip'
$taskFolder='GripFrames'
$taskMode='GripNetwork'
$taskFrames=Join-Path $taskRoot ('Saved\'+$taskFolder)
if (-not $ReuseFrames) {
    New-Item -ItemType Directory -Path $taskFrames -Force | Out-Null
    Get-ChildItem -LiteralPath $taskFrames -Filter 'Grip_*.png' -File | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
    & "$PSScriptRoot\Test.ps1" -EngineRoot $EngineRoot -Mode $taskMode -PacketLagMs 75 -PacketLoss 2 -CaptureGrip
}
$taskCulture=[Globalization.CultureInfo]::InvariantCulture
$taskRows=@(Get-Content -LiteralPath (Join-Path $taskFrames 'times.csv') | Where-Object { $_ } | ForEach-Object {
    $taskParts=$_ -split ','
    [PSCustomObject]@{ Name=$taskParts[0]; Time=[double]::Parse($taskParts[1],$taskCulture) }
})
if ($taskRows.Count -lt 2) { throw 'No timed grip capture to encode.' }
$taskLines=[Collections.Generic.List[string]]::new()
for ($taskI=0;$taskI -lt $taskRows.Count;$taskI++) {
    if (-not (Test-Path -LiteralPath (Join-Path $taskFrames $taskRows[$taskI].Name))) { throw 'A captured frame is missing.' }
    $taskDuration=if($taskI+1 -lt $taskRows.Count){ $taskRows[$taskI+1].Time-$taskRows[$taskI].Time }else{ .1 }
    $taskLines.Add("file '$($taskRows[$taskI].Name)'")
    $taskLines.Add('duration '+$taskDuration.ToString('F6',$taskCulture))
}
$taskLines.Add("file '$($taskRows[-1].Name)'")
[IO.File]::WriteAllLines((Join-Path $taskFrames 'timing.txt'),$taskLines)
$taskCaptions=@'
[Script Info]
ScriptType: v4.00+
PlayResX: 1280
PlayResY: 720
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,27,&H00FFFFFF,&H00FFFFFF,&H00202020,&H80202020,0,0,0,0,100,100,0,0,3,2,0,2,30,30,28,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,0:00:03.00,Default,,0,0,0,,01  Две руки: подвод к поверхности
Dialogue: 0,0:00:03.00,0:00:06.00,Default,,0,0,0,,02  Толкание перед собой
Dialogue: 0,0:00:06.00,0:00:09.00,Default,,0,0,0,,03  Боковой хват ближней рукой
Dialogue: 0,0:00:09.00,0:00:12.00,Default,,0,0,0,,04  Тяга двумя руками за спиной
Dialogue: 0,0:00:12.00,0:00:15.00,Default,,0,0,0,,05  Плавное отпускание
Dialogue: 0,0:00:15.00,0:00:22.50,Default,,0,0,0,,06  Повторный хват, удар и ragdoll
'@
[IO.File]::WriteAllText((Join-Path $taskFrames 'captions.ass'),$taskCaptions,[Text.UTF8Encoding]::new($false))
Push-Location $taskFrames
try {
    & $FFmpeg -hide_banner -loglevel warning -y -f concat -safe 0 -i timing.txt -vf 'fps=30,subtitles=captions.ass' -c:v libx264 -crf 20 -pix_fmt yuv420p -movflags +faststart "$taskRoot\Artifacts\$taskName.mp4"
} finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw 'Grip recording encoding failed.' }
$taskCoverTime='3'
& $FFmpeg -hide_banner -loglevel warning -y -ss $taskCoverTime -i "$taskRoot\Artifacts\$taskName.mp4" -frames:v 1 -update 1 "$taskRoot\Artifacts\$taskName.png"
if ($LASTEXITCODE -ne 0) { throw 'Grip cover failed.' }
