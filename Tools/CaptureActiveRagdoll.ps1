param([string]$EngineRoot='E:\UE\UE_5.8',[string]$FFmpeg='C:\ffmpeg\ffmpeg.exe',[switch]$ReuseFrames)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $ReuseFrames){ & "$PSScriptRoot\TestActiveRagdoll.ps1" -EngineRoot $EngineRoot -Solo -Review }
$taskFrames=Join-Path $taskRoot 'Saved\ApprovalFrames\ActiveRagdoll'
$taskCulture=[Globalization.CultureInfo]::InvariantCulture
$taskRows=@(Get-Content -LiteralPath (Join-Path $taskFrames 'times.csv') | Where-Object {$_} | ForEach-Object {
    $taskParts=$_ -split ','
    # A numbered review still can replace the video screenshot requested that frame.
    if(Test-Path -LiteralPath (Join-Path $taskFrames $taskParts[0])) {
        [PSCustomObject]@{Name=$taskParts[0];Time=[double]::Parse($taskParts[1],$taskCulture)}
    }
})
if($taskRows.Count -lt 2){throw 'No timed active ragdoll capture to encode.'}
$taskLines=[Collections.Generic.List[string]]::new()
for($taskIndex=0;$taskIndex -lt $taskRows.Count;$taskIndex++) {
    $taskDuration=if($taskIndex+1 -lt $taskRows.Count){$taskRows[$taskIndex+1].Time-$taskRows[$taskIndex].Time}else{.1}
    if($taskDuration -le 0){throw 'Capture timestamps must increase; use a fresh capture directory for another recording.'}
    $taskLines.Add("file '$($taskRows[$taskIndex].Name)'")
    $taskLines.Add('duration '+$taskDuration.ToString('F6',$taskCulture))
}
$taskLines.Add("file '$($taskRows[-1].Name)'")
[IO.File]::WriteAllLines((Join-Path $taskFrames 'timing.txt'),$taskLines)
Push-Location $taskFrames
try {
    & $FFmpeg -hide_banner -loglevel warning -y -f concat -safe 0 -i timing.txt -vf fps=30 -c:v libx264 -crf 19 -pix_fmt yuv420p -movflags +faststart (Join-Path $taskRoot 'Artifacts\ActiveRagdoll\Comparison.mp4')
} finally {Pop-Location}
if($LASTEXITCODE -ne 0){throw 'Active ragdoll recording encoding failed.'}
