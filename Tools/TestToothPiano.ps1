param([int]$PacketLagMs=0)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor='C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskReports=Join-Path $taskRoot "Saved\ToothPianoTests\Network${PacketLagMs}"
New-Item -ItemType Directory -Path $taskReports -Force | Out-Null
$taskProcesses=@()
try {
    for($taskIndex=0;$taskIndex -lt 2;$taskIndex++) {
        $taskMap=if($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'127.0.0.1:7777'}
        $taskLog=Join-Path $taskReports "Peer${taskIndex}.log"
        $taskArgs=@("`"$taskRoot\MessControl.uproject`"",$taskMap,'-game','-nosteam','-MCLegacyDays','-MCToothPianoTest','-nullrhi','-unattended','-nosound','-nosplash','-nop4','-DisablePython','-ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0"',"`"-abslog=$taskLog`"")
        if($PacketLagMs -gt 0){$taskArgs+="-PktLag=$PacketLagMs"}
        $taskProcesses+=Start-Process -FilePath $taskEditor -ArgumentList $taskArgs -WindowStyle Hidden -PassThru
        if($taskIndex -eq 0) {
            $taskDeadline=(Get-Date).AddSeconds(30)
            do {
                Start-Sleep -Milliseconds 250
                if($taskProcesses[0].HasExited){throw 'Piano host exited before listening'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern 'listening on port 7777' -Quiet)
            } until($taskListening -or (Get-Date) -gt $taskDeadline)
            if(-not $taskListening){throw 'Piano host did not start listening'}
        }
    }
    foreach($taskProcess in $taskProcesses) {
        if(-not $taskProcess.WaitForExit(60000)){throw 'Piano network test timed out'}
        if($taskProcess.ExitCode -ne 0){throw "Piano peer exited with $($taskProcess.ExitCode)"}
    }
    foreach($taskIndex in 0,1) {
        $taskLog=Join-Path $taskReports "Peer${taskIndex}.log"
        $taskMarkers=@(Select-String -LiteralPath $taskLog -Pattern 'MC_PIANO_(PASS|FAIL)')
        $taskMarkers.Line | Write-Output
        if(-not ($taskMarkers.Line -match 'MC_PIANO_PASS') -or ($taskMarkers.Line -match 'MC_PIANO_FAIL')){throw "Piano peer $taskIndex failed"}
    }
} finally {
    foreach($taskProcess in $taskProcesses){if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
}
