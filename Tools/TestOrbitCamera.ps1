param(
    [string]$BuildRoot,
    [string]$EngineRoot=$env:UE_ROOT,
    [switch]$Capture,
    [switch]$RadialReveal
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if($BuildRoot) {
    $taskExe=Join-Path $BuildRoot 'MessControl/Binaries/Win64/MessControl.exe'
    $taskReports=Join-Path (Split-Path $BuildRoot -Parent) 'CameraValidation'
    $taskPrefix=@()
} else {
    if(-not $EngineRoot) {
        $taskInstalls=(Get-Content -Raw 'C:/ProgramData/Epic/UnrealEngineLauncher/LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
        $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
    }
    $taskExe=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $taskReports=Join-Path $taskRoot $(if($RadialReveal){'Saved/CameraRevealValidation'}else{'Saved/OrbitCameraValidation'})
    $taskPrefix=@("`"$taskRoot/MessControl.uproject`"",'/Game/Maps/L_Mouth','-game')
}
New-Item -ItemType Directory -Path $taskReports -Force | Out-Null
$taskLog=Join-Path $taskReports 'Orbit.log'
$taskFlag=if($RadialReveal){'-MCCameraRevealTest'}else{'-MCOrbitCameraTest'}
$taskMarker=if($RadialReveal){'MC_REVEAL'}else{'MC_ORBIT'}
$taskArgs=$taskPrefix+@('-nosteam',$taskFlag,'-DisablePython','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-unattended','-nosound','-nosplash',"`"-abslog=$taskLog`"",'-ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0"')
if($Capture){$taskArgs+='-MCOrbitCameraCapture'}
$taskProcess=Start-Process -FilePath $taskExe -WorkingDirectory (Split-Path $taskExe -Parent) -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
try {
    if(-not $taskProcess.WaitForExit(90000)){throw 'Orbit camera test timed out.'}
    $taskText=Get-Content -Raw -LiteralPath $taskLog
    $taskMarks=@($taskText -split "`n" | Where-Object {$_ -match "${taskMarker}_(PASS|FAIL)"})
    # The editor's optional GameFeatures plugin already reports this unrelated startup configuration error.
    # Retain it in the report; any other engine error still fails the camera run.
    $taskErrors=@($taskText -split "`n" | Where-Object {$_ -match ': Error:|Fatal error:'})
    $taskStartupErrors=@($taskErrors | Where-Object {$_ -match 'LogGameFeatures: Error: Asset manager settings do not include a rule for assets of type GameFeatureData, which is required for game feature plugins to function'})
    $taskUnexpectedErrors=@($taskErrors | Where-Object {$_ -notin $taskStartupErrors})
    $taskOk=$taskProcess.ExitCode -eq 0 -and $taskText -match "${taskMarker}_PASS" -and $taskText -notmatch "${taskMarker}_FAIL" -and $taskUnexpectedErrors.Count -eq 0
    @{passed=$taskOk;exitCode=$taskProcess.ExitCode;markers=$taskMarks;startupErrors=$taskStartupErrors;errors=$taskUnexpectedErrors;log=$taskLog} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskReports 'Results.json') -Encoding utf8
    $taskMarks | ForEach-Object {Write-Output $_}
    if(-not $taskOk){throw "Orbit camera test failed: $taskLog"}
} finally {if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
