param([string]$EngineRoot='E:\UE\UE_5.8',[switch]$Capture)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$taskProcesses=@()
$taskLogs=@()
try {
    for ($taskIndex=0;$taskIndex -lt 4;$taskIndex++) {
        # Join after the first local brush trail exists, exercising initial replication.
        if ($taskIndex -eq 3) { Start-Sleep -Seconds 16 }
        $taskLog=Join-Path $taskRoot "Saved/Logs/CareNetwork$taskIndex.log"
        $taskLogs+=$taskLog
        if (Test-Path -LiteralPath $taskLog) { Remove-Item -LiteralPath $taskLog }
        $taskMap=if($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'127.0.0.1:7783'}
        $taskArgs=@("`"$taskRoot\MessControl.uproject`"",$taskMap,'-game','-MCCareTest','-port=7783','-nullrhi','-unattended','-nosound','-nosplash','-nop4','-PktLag=60','-PktLoss=1','-ExecCmds="t.MaxFPS 60"',"`"-abslog=$taskLog`"")
        if ($Capture -and $taskIndex -eq 0) {
            $taskArgs=@($taskArgs | Where-Object {$_ -ne '-nullrhi' -and $_ -notlike '-ExecCmds=*'})
            $taskArgs+=@('-MCCareCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1600','-ResY=1000','-NoScreenMessages','-ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0,r.ScreenPercentage 100"')
        }
        $taskProcesses+=Start-Process -FilePath $taskEditor -WindowStyle Hidden -PassThru -ArgumentList $taskArgs
        if ($taskIndex -eq 0) {
            $taskDeadline=(Get-Date).AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                if ($taskProcesses[0].HasExited) {throw 'Care host exited before listening.'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern 'listening on port 7783' -Quiet)
            } until ($taskListening -or (Get-Date) -gt $taskDeadline)
            if (-not $taskListening) {throw 'Care host did not start listening.'}
        }
    }
    foreach ($taskProcess in $taskProcesses) { if (-not $taskProcess.WaitForExit(120000)) {throw 'Care test timed out.'} }
    $taskHashes=@();$taskResults=@()
    foreach ($taskLog in $taskLogs) {
        $taskPass=Select-String -LiteralPath $taskLog -Pattern 'MC_CARE_PASS.*hash=(\d+)'
        if (-not $taskPass) {throw "Care test failed: $taskLog"}
        $taskHashes+=$taskPass.Matches[0].Groups[1].Value
        $taskResults+=$taskPass.Line
    }
    if (@($taskHashes | Select-Object -Unique).Count -ne 1) {throw 'Tooth masks differ across players.'}
    @{passed=$true;latencyMs=60;packetLossPercent=1;lateJoin=$true;results=$taskResults} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$taskRoot/Artifacts/Care_Network.json"
    Write-Output 'PASS: local cleaning, cooperative contacts and late join agree in four processes.'
} finally {
    foreach ($taskProcess in $taskProcesses) { if (-not $taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id} }
}
