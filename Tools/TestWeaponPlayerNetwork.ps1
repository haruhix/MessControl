param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[ValidateRange(0,250)][int]$PacketLagMs=75,[ValidateRange(0,10)][int]$PacketLoss=2)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProcesses=@()
try {
    for($taskIndex=0;$taskIndex -lt 4;$taskIndex++) {
        $taskMap=if($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'127.0.0.1:7777'}
        $taskLog=Join-Path $taskRoot "Saved\Logs\WeaponPvP$taskIndex.log"
        if(Test-Path -LiteralPath $taskLog){Remove-Item -LiteralPath $taskLog}
        $taskArgs=@("`"$taskRoot\MessControl.uproject`"",$taskMap,'-game','-nosteam','-MCLegacyDays','-MCWeaponPvP','-nullrhi','-unattended','-nosound','-nosplash','-nop4',"-PktLag=$PacketLagMs","-PktLoss=$PacketLoss",'-ExecCmds="t.MaxFPS 60"',"`"-abslog=$taskLog`"")
        $taskProcesses+=Start-Process -FilePath $taskEditor -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
        if($taskIndex -eq 0) {
            $taskListenDeadline=(Get-Date).AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                if($taskProcesses[0].HasExited){throw 'Weapon PvP host exited before listening.'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern 'listening on port 7777' -Quiet)
            } until($taskListening -or (Get-Date) -gt $taskListenDeadline)
            if(-not $taskListening){throw 'Weapon PvP host did not start listening.'}
        }
    }
    $taskDeadline=(Get-Date).AddSeconds(120)
    while(@($taskProcesses | Where-Object { -not $_.HasExited }).Count -gt 0) {
        if((Get-Date) -gt $taskDeadline){throw 'Weapon PvP network test timed out.'}
        Start-Sleep -Milliseconds 500
    }
    for($taskIndex=0;$taskIndex -lt 4;$taskIndex++) {
        $taskLog=Join-Path $taskRoot "Saved\Logs\WeaponPvP$taskIndex.log"
        if($taskProcesses[$taskIndex].ExitCode -ne 0 -or -not (Select-String -LiteralPath $taskLog -Pattern 'MC_VALIDATION_PASS WEAPON_PVP' -Quiet)) {throw "Weapon PvP failed for peer $taskIndex. See $taskLog"}
    }
    Write-Output 'PASS: remote brush, pickaxe and knife hits replicate once to four peers; spray deals no player damage.'
} finally {
    foreach($taskProcess in $taskProcesses) {if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
}
