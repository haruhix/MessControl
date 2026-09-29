param(
 [Parameter(Mandatory=$true)][string]$BuildRoot,
 [ValidateSet('Startup','Launcher','Camera','Brush','BrushMove','Coverage','Throat','Swim','Tongue','Jolt')][string[]]$Cases=@('Startup','Launcher','Camera','Brush','BrushMove','Coverage','Throat','Swim','Tongue','Jolt'),
 [string]$ReportDirectory
)
$ErrorActionPreference='Stop'
$taskExe=Join-Path $BuildRoot 'MessControl/Binaries/Win64/MessControl.exe'
$taskReports=if($ReportDirectory){[IO.Path]::GetFullPath($ReportDirectory)}else{Join-Path (Split-Path $BuildRoot -Parent) 'Validation'}
New-Item -ItemType Directory -Path $taskReports -Force | Out-Null
if(-not(Test-Path -LiteralPath $taskExe)){throw "Packaged executable missing: $taskExe"}
$taskResults=@()
foreach($taskCase in $Cases){
 $taskLog=Join-Path $taskReports ($taskCase+'.log')
 $taskFps=if($taskCase -in @('Brush','BrushMove')){120}elseif($taskCase -in @('Camera','Coverage')){30}else{60}
 $taskFlags=switch($taskCase){
  Startup {@('-Seconds=15')}
  Launcher {@('-Seconds=15')}
  Camera {@('-MCCameraTest','-MCCameraCapture')}
  Brush {@('-MCBrushTest')}
  BrushMove {@('-MCBrushTest','-MCBrushMotion')}
  Coverage {@('-MCBrushTest','-MCBrushCoverage')}
  Throat {@('-MCThroatTest')}
  Swim {@('-MCSwimTest')}
  Tongue {@('-MCTongueTest')}
  Jolt {@('-MCTongueTest','-MCTongueJolt')}
  default {throw "Unknown case $taskCase"}
 }
 $taskPass=switch($taskCase){
  Startup {'LogExit: Exiting\.'}
  Launcher {'LogExit: Exiting\.'}
  Camera {'MC_CAMERA_PASS'}
  Brush {'MC_BRUSH_PASS'}
  BrushMove {'MC_BRUSH_PASS'}
  Coverage {'MC_BRUSH_COVERAGE_PASS'}
  Throat {'MC_VALIDATION_PASS THROAT'}
  Swim {'MC_VALIDATION_PASS SWIM'}
  Tongue {'MC_VALIDATION_PASS TONGUE'}
  Jolt {'MC_VALIDATION_PASS TONGUE'}
 }
 $taskArgs=@('/Game/Maps/L_Mouth?Seed=41','-MCExpectedPlayers=1','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-unattended','-nosplash','-nop4','-NoScreenMessages',"`"-abslog=$taskLog`"",'-UseFixedTimeStep',"-FPS=$taskFps", "`"-ExecCmds=t.MaxFPS $taskFps,t.IdleWhenNotForeground 0,r.ScreenPercentage 100`"")+$taskFlags
 if($taskCase -notin @('Startup','Launcher')){$taskArgs+=@('-MCLegacyDays','-nosound')}
 $taskLaunchExe=$taskExe
 if($taskCase -eq 'Launcher'){
  $taskLaunchExe=Join-Path $BuildRoot 'MessControl.exe'
  # Exercise the distributed launcher and the configured default map.
  $taskArgs=@($taskArgs | Where-Object {$_ -notlike '/Game/Maps/*'})
 }
 $taskStart=Get-Date
 Write-Output "START $taskCase ($taskFps FPS), packaged executable"
 $taskProcess=Start-Process -FilePath $taskLaunchExe -WorkingDirectory (Split-Path $taskLaunchExe -Parent) -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
 $taskTimedOut=$false
 $taskMemory=@()
 try {
  while(-not $taskProcess.WaitForExit(1000)){
   $taskProcess.Refresh()
   $taskMemory+=@{seconds=[math]::Round(((Get-Date)-$taskStart).TotalSeconds,2);privateMiB=[math]::Round($taskProcess.PrivateMemorySize64/1MB,1);workingSetMiB=[math]::Round($taskProcess.WorkingSet64/1MB,1)}
   if(((Get-Date)-$taskStart).TotalSeconds -gt 240){$taskTimedOut=$true;Stop-Process -Id $taskProcess.Id;break}
  }
  $taskExit=$taskProcess.ExitCode
 } finally {if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
 $taskText=if(Test-Path -LiteralPath $taskLog){Get-Content -Raw -LiteralPath $taskLog}else{''}
 $taskErrors=@($taskText -split "`n" | Where-Object {$_ -match 'Fatal error:|: Error:|Failed to find object|Couldn.t find file for package|Could not find a package file|Failed to load package'})
 $taskOk=(-not $taskTimedOut -and $taskExit -eq 0 -and $taskText -match $taskPass -and $taskErrors.Count -eq 0)
 $taskMarks=@($taskText -split "`n" | Where-Object {$_ -match 'MC_.*(PASS|FAIL)|MC_UVULA_ANIMATION|MC_BRUSH_(FREE_HAND|WORK_HAND|MOTION)|MC_CAMERA_VIEW'})
 $taskMemory | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskReports ($taskCase+'_memory.json')) -Encoding utf8
 $taskRow=@{case=$taskCase;fps=$taskFps;passed=$taskOk;exitCode=$taskExit;timedOut=$taskTimedOut;seconds=[math]::Round(((Get-Date)-$taskStart).TotalSeconds,2);peakPrivateMiB=($taskMemory.privateMiB | Measure-Object -Maximum).Maximum;log=$taskLog;errors=$taskErrors;markers=$taskMarks;packaged=$true;network=$false}
 $taskResults+=$taskRow
 $taskResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskReports 'Results.json') -Encoding utf8
 $taskMarks | ForEach-Object {Write-Output $_}
 Write-Output "RESULT $taskCase passed=$taskOk exit=$taskExit errors=$($taskErrors.Count)"
 if(-not $taskOk){$taskErrors | Select-Object -First 12 | Write-Output;throw "Packaged $taskCase failed; see $taskLog"}
}
