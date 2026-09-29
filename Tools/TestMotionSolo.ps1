param(
    [string]$EngineRoot='E:\UE\UE_5.8',
    [ValidateSet('Brush','BrushMove','Swim','Tongue','Jolt','Throat')][string[]]$Cases=@('Brush','BrushMove','Swim','Tongue','Jolt','Throat'),
    [ValidateSet(30,60,120)][int]$FrameRate=60,
    [switch]$CaptureBrush
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskExe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskResults=@()
$taskResultPath=Join-Path $taskRoot "Artifacts\MotionStability\results_$FrameRate.json"
if(Test-Path -LiteralPath $taskResultPath) { $taskResults=@(Get-Content -Raw -LiteralPath $taskResultPath | ConvertFrom-Json) }
foreach($taskCase in $Cases) {
    $taskLabel="solo_$($taskCase.ToLower())_$FrameRate"
    $taskLog=Join-Path $taskRoot "Saved\Logs\$taskLabel.log"
    $taskRunner=Join-Path $taskRoot "Saved\Logs\${taskLabel}_runner.log"
    $taskFlag=switch($taskCase) {
        Brush {'-MCBrushTest'} BrushMove {'-MCBrushTest'; '-MCBrushMotion'} Swim {'-MCSwimTest'}
        Tongue {'-MCTongueTest'} Jolt {'-MCTongueTest'; '-MCTongueJolt'} Throat {'-MCThroatTest'}
    }
    $taskPass=switch($taskCase) {
        Brush {'MC_BRUSH_PASS'} BrushMove {'MC_BRUSH_PASS'} Swim {'MC_VALIDATION_PASS SWIM'}
        Tongue {'MC_VALIDATION_PASS TONGUE'} Jolt {'MC_VALIDATION_PASS TONGUE'} Throat {'MC_VALIDATION_PASS THROAT'}
    }
    # Always standalone: this runner has no listen URL, client or packet options.
    $taskArgs=@("$taskRoot\MessControl.uproject",'/Game/Maps/L_Mouth?Seed=41','-game','-MCLegacyDays','-MCExpectedPlayers=1')+$taskFlag
    $taskArgs+=@('-RenderOffscreen','-windowed','-ForceRes','-ResX=960','-ResY=640','-unattended','-nosound','-nosplash','-nop4','-NoScreenMessages','-UseFixedTimeStep',"-FPS=$FrameRate",
        "-ExecCmds=t.MaxFPS $FrameRate,t.IdleWhenNotForeground 0,mc.Anim.Record 110 $taskLabel","-abslog=$taskLog")
    if($CaptureBrush -and $taskCase -eq 'Brush') {$taskArgs+='-MCBrushCapture'}
    $taskStarted=Get-Date
    & $taskExe @taskArgs *> $taskRunner
    $taskExit=$LASTEXITCODE
    $taskMatch=Select-String -LiteralPath $taskLog -Pattern $taskPass | Select-Object -Last 1
    $taskCSV=Get-ChildItem -LiteralPath (Join-Path $taskRoot 'Saved\MotionDiagnostics') -Filter "${taskLabel}_*.csv" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $taskReport=Join-Path $taskRoot "Artifacts\MotionStability\${taskLabel}.json"
    if(-not $taskCSV -or $taskCSV.LastWriteTime -lt $taskStarted) {throw "No fresh motion recording for $taskCase"}
    & python "$PSScriptRoot\Unreal\analyze_motion.py" $taskCSV.FullName --output $taskReport
    if($LASTEXITCODE -ne 0) {throw "Motion analysis failed for $taskCase"}
    $taskResults=@($taskResults | Where-Object {$_.case -ne $taskCase})
    $taskResults+=@{case=$taskCase;fps=$FrameRate;standalone=$true;passed=($taskExit -eq 0 -and $null -ne $taskMatch);log=$taskLog;trace=$taskCSV.FullName;analysis=$taskReport}
    if($taskMatch) { $taskMatch.Line }
    $taskResults | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $taskResultPath -Encoding utf8
    if($taskExit -ne 0 -or -not $taskMatch) {throw "$taskCase solo motion validation failed: $taskLog"}
}
