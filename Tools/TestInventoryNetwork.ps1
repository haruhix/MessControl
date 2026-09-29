param([string]$EngineRoot='E:\UE\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskProcesses=@()
try {
    for($taskIndex=0;$taskIndex -lt 2;$taskIndex++) {
        $taskMap=if($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'127.0.0.1:7777'}
        $taskLog=Join-Path $taskRoot "Saved\Logs\InventoryNet$taskIndex.log"
        if(Test-Path -LiteralPath $taskLog){Remove-Item -LiteralPath $taskLog}
        $taskArgs=@("`"$taskRoot\MessControl.uproject`"",$taskMap,'-game','-MCLegacyDays','-MCInventoryTest','-MCInventoryNetworkTest','-nullrhi','-unattended','-nosound','-nosplash','-nop4','-PktLag=75','-PktLoss=2',"`"-abslog=$taskLog`"",'"-ExecCmds=t.MaxFPS 60"')
        $taskProcesses+=Start-Process (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
        if($taskIndex -eq 0) {
            $taskDeadline=(Get-Date).AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                if($taskProcesses[0].HasExited){throw 'Inventory host exited before listening.'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern 'listening on port 7777' -Quiet)
            } until($taskListening -or (Get-Date) -gt $taskDeadline)
            if(-not $taskListening){throw 'Inventory host did not start listening.'}
        }
    }
    foreach($taskProcess in $taskProcesses){if(-not $taskProcess.WaitForExit(90000)){throw 'Inventory network test timed out.'}}
    $taskResults=for($taskIndex=0;$taskIndex -lt 2;$taskIndex++) {
        $taskLog=Join-Path $taskRoot "Saved\Logs\InventoryNet$taskIndex.log"
        $taskChecks=Select-String -LiteralPath $taskLog -Pattern 'MC_INVENTORY_NET_(CHECK|PASS|FAIL)'
        $taskChecks.Line
        if($taskProcesses[$taskIndex].ExitCode -ne 0 -or -not ($taskChecks.Line -match 'MC_INVENTORY_NET_PASS')){throw "Inventory network validation failed: $taskLog"}
    }
    $taskResults | Set-Content -LiteralPath (Join-Path $taskRoot 'Artifacts\InventoryNetwork_Validation.txt') -Encoding utf8
    $taskResults
} finally { foreach($taskProcess in $taskProcesses){if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}} }
