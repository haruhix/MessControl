param([string]$BuildRoot,[string]$EngineRoot=$env:UE_ROOT,[int]$Port=17807,[switch]$Capture)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if($BuildRoot) {
    $taskExe=Join-Path $BuildRoot 'MessControl/Binaries/Win64/MessControl.exe'
    $taskPrefix=@()
    $taskReports=Join-Path (Split-Path $BuildRoot -Parent) 'PlayerNameValidation'
} else {
    if(-not $EngineRoot) {$EngineRoot='C:\Program Files\Epic Games\UE_5.8'}
    $taskExe=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $taskPrefix=@("`"$taskRoot/MessControl.uproject`"",'-game')
    $taskReports=Join-Path $taskRoot 'Saved/PlayerNameValidation'
}
New-Item -ItemType Directory -Path $taskReports -Force | Out-Null
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) {throw "UDP port $Port is already in use"}
$taskProcesses=@()
$taskResults=@()
try {
    for($taskIndex=0;$taskIndex -lt 2;$taskIndex++) {
        $taskMap=if($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{"127.0.0.1:$Port"}
        $taskLog=Join-Path $taskReports "Player$taskIndex.log"
        $taskArgs=$taskPrefix+@($taskMap,'-nosteam','-MCLegacyDays','-MCPlayerNameTest','-DisablePython','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-unattended','-nosplash','-nosound','-PktLag=75','-PktLoss=2',"`"-abslog=$taskLog`"",'-ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0"')
        if($taskIndex -eq 0) {$taskArgs+="-port=$Port"}
        if($Capture) {$taskArgs+='-MCPlayerNameCapture'}
        $taskProcesses+=Start-Process -FilePath $taskExe -WorkingDirectory (Split-Path $taskExe -Parent) -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
        if($taskIndex -eq 0) {
            $taskDeadline=(Get-Date).AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                if($taskProcesses[0].HasExited) {throw 'Name test host exited before listening'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern "listening on port $Port" -Quiet)
            } until($taskListening -or (Get-Date) -gt $taskDeadline)
            if(-not $taskListening) {throw 'Name test host did not start listening'}
        }
    }
    for($taskIndex=0;$taskIndex -lt 2;$taskIndex++) {
        $taskProcess=$taskProcesses[$taskIndex]
        if(-not $taskProcess.WaitForExit(90000)) {throw 'Player name test timed out'}
        $taskLog=Join-Path $taskReports "Player$taskIndex.log"
        $taskText=Get-Content -Raw -LiteralPath $taskLog
        $taskMarkers=@($taskText -split "`n" | Where-Object {$_ -match 'MC_NAMES_(PASS|FAIL)'})
        $taskErrors=@($taskText -split "`n" | Where-Object {$_ -match 'Fatal error:|: Error:' -and $_ -notmatch 'LogGameFeatures: Error: Asset manager settings do not include a rule for assets of type GameFeatureData'})
        $taskPassed=$taskProcess.ExitCode -eq 0 -and $taskText -match 'MC_NAMES_PASS' -and $taskText -notmatch 'MC_NAMES_FAIL' -and $taskErrors.Count -eq 0
        $taskResults+=@{player=$taskIndex;passed=$taskPassed;exitCode=$taskProcess.ExitCode;markers=$taskMarkers;errors=$taskErrors;log=$taskLog;packaged=[bool]$BuildRoot}
        $taskResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskReports 'Results.json') -Encoding utf8
        $taskMarkers | Write-Output
        if(-not $taskPassed) {throw "Player name validation failed: $taskLog"}
    }
} finally {foreach($taskProcess in $taskProcesses) {if(-not $taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id}}}
