param(
    [string]$EngineRoot=$env:UE_ROOT,
    [ValidateSet('Menu','Tutorial','TutorialNetwork','LobbyNetwork')][string]$Mode='Menu',
    [ValidateRange(2,4)][int]$NetworkPlayers=4,
    [switch]$Capture
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw -LiteralPath 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProject=Join-Path $taskRoot 'MessControl.uproject'
$taskLogs=Join-Path $taskRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $taskLogs -Force | Out-Null
$taskNetwork=$Mode -in @('TutorialNetwork','LobbyNetwork')
$taskCount=if($taskNetwork){$NetworkPlayers}else{1}
$taskPass=if($Mode -eq 'Menu'){'MC_FRONTEND_PASS'}elseif($Mode -eq 'LobbyNetwork'){'MC_LOBBY_PASS'}else{'MC_TUTORIAL_PASS'}
$taskFlag=if($Mode -eq 'Menu'){'-MCFrontEndSmoke'}elseif($Mode -eq 'LobbyNetwork'){'-MCLobbySmoke'}else{'-MCTutorialSmoke'}
$taskProcesses=@()
try {
    for($taskIndex=0;$taskIndex -lt $taskCount;$taskIndex++) {
        $taskMap=if($taskIndex -gt 0){'127.0.0.1:7777'}elseif($Mode -eq 'Menu'){'/Game/Maps/L_MainMenu'}elseif($Mode -eq 'LobbyNetwork'){'/Game/Maps/L_Mouth?listen?MCLobby=1?Seed=41'}elseif($taskNetwork){'/Game/Maps/L_Mouth?listen?MCTutorial=1?Seed=41'}else{'/Game/Maps/L_Mouth?MCTutorial=1?Seed=41'}
        $taskLog=Join-Path $taskLogs "$Mode$taskIndex.log"
        if(Test-Path -LiteralPath $taskLog){Remove-Item -LiteralPath $taskLog}
        $taskArgs=@("`"$taskProject`"",$taskMap,'-game','-nosteam',$taskFlag,"-MCTutorialSmokePlayers=$taskCount",'-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"")
        if($Capture -and $taskIndex -eq 0) {
            $taskArgs+=@('-MCFrontEndCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','-ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,sg.PostProcessQuality 1,r.ScreenPercentage 100"')
        } else {
            $taskArgs+=@('-nullrhi','-ExecCmds="t.MaxFPS 60"')
        }
        $taskProcesses+=Start-Process -FilePath $taskEditor -WindowStyle Hidden -PassThru -ArgumentList $taskArgs
        if($taskNetwork -and $taskIndex -eq 0) {
            $taskListenDeadline=(Get-Date).AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                if($taskProcesses[0].HasExited){throw 'Host exited before listening.'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern 'listening on port 7777' -Quiet)
            } until($taskListening -or (Get-Date) -gt $taskListenDeadline)
            if(-not $taskListening){throw 'Host did not start listening in time.'}
        }
    }
    foreach($taskProcess in $taskProcesses) {
        if(-not $taskProcess.WaitForExit(160000)){throw "$Mode timed out. See Saved/Logs/$Mode*.log"}
        if($taskProcess.ExitCode -ne 0){throw "$Mode process $($taskProcess.Id) failed. See Saved/Logs/$Mode*.log"}
    }
    for($taskIndex=0;$taskIndex -lt $taskCount;$taskIndex++) {
        $taskLog=Join-Path $taskLogs "$Mode$taskIndex.log"
        if(-not (Select-String -LiteralPath $taskLog -Pattern $taskPass -Quiet)){throw "$Mode peer $taskIndex has no passing marker."}
        if(Select-String -LiteralPath $taskLog -Pattern 'MC_(FRONTEND|TUTORIAL|LOBBY)_FAIL' -Quiet){throw "$Mode peer $taskIndex reported a failure."}
    }
    Write-Output "PASS: $Mode ($taskCount processes)."
} finally {
    foreach($taskProcess in $taskProcesses) {if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
}
