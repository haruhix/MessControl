param([string]$EngineRoot='E:\UE\UE_5.8',[ValidateSet(30,60,120)][int]$FrameRate=60,[switch]$Capture)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskLog=Join-Path $taskRoot "Saved\Logs\BrushCoverage$FrameRate.log"
$taskExe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskArgs=@("$taskRoot\MessControl.uproject",'/Game/Maps/L_Mouth?Seed=41','-game','-MCLegacyDays','-MCBrushTest','-MCBrushCoverage','-MCExpectedPlayers=1',
    '-RenderOffscreen','-windowed','-ForceRes','-ResX=960','-ResY=640','-unattended','-nosound','-nosplash','-nop4','-NoScreenMessages','-UseFixedTimeStep',"-FPS=$FrameRate",
    "-ExecCmds=t.MaxFPS $FrameRate,t.IdleWhenNotForeground 0,Trace.Disable Screenshot","-abslog=$taskLog")
if($Capture){$taskArgs+='-MCBrushCapture'}
$taskStarted=Get-Date
& $taskExe @taskArgs *> (Join-Path $taskRoot 'Saved\Logs\BrushCoverage_runner.log')
$taskExit=$LASTEXITCODE
$taskLines=Select-String -LiteralPath $taskLog -Pattern 'MC_BRUSH_COVERAGE (case=)|MC_BRUSH_COVERAGE_(PASS|FAIL)'
$taskLines.Line
$taskFolder=Join-Path $taskRoot 'Artifacts\BrushCoverage'
New-Item -ItemType Directory -Path $taskFolder -Force | Out-Null
$taskLines.Line | Set-Content -LiteralPath (Join-Path $taskFolder "Coverage$FrameRate.txt") -Encoding utf8
if($taskExit -ne 0 -or -not (Select-String -LiteralPath $taskLog -Pattern 'MC_BRUSH_COVERAGE_PASS' -Quiet)){throw "Brush coverage failed: $taskLog"}
if($Capture) {
    $taskFrames=@(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'Saved\BrushCoverageFrames') -Filter '*.png' | Where-Object {$_.LastWriteTime -ge $taskStarted} | Sort-Object Name)
    $taskList=Join-Path $taskRoot 'Saved\BrushCoverageFrames\current.txt'
    $taskFrames | ForEach-Object { "file '$($_.FullName.Replace('\','/'))'"; 'duration 0.033333333' } | Set-Content -LiteralPath $taskList -Encoding utf8NoBOM
    & ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i $taskList -vf fps=30 -c:v libx264 -crf 19 -pix_fmt yuv420p -movflags +faststart (Join-Path $taskFolder "Coverage$FrameRate.mp4")
    if($LASTEXITCODE -ne 0){throw 'Coverage video encoding failed'}
}
