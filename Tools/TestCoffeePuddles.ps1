param([string]$EngineRoot='E:\UE\UE_5.8',[switch]$Capture)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProcesses=@()
$taskLogs=@()
try {
    for ($taskIndex=0;$taskIndex -lt 4;$taskIndex++) {
        $taskLog=Join-Path $taskRoot "Saved\Logs\PuddleNetwork$taskIndex.log"
        $taskLogs+=$taskLog
        $taskMap=if($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'127.0.0.1:7779'}
        $taskArgs=@("`"$taskRoot\MessControl.uproject`"",$taskMap,'-game','-MCLegacyDays','-MCDayOne','-MCLiquidTest','-port=7779','-nullrhi','-unattended','-nosound','-nosplash','-nop4','-ExecCmds="t.MaxFPS 60"',"`"-abslog=$taskLog`"")
        if ($Capture -and $taskIndex -eq 0) {
            $taskArgs=@($taskArgs | Where-Object {$_ -ne '-nullrhi' -and $_ -notlike '-ExecCmds=*'})
            $taskArgs+=@('-MCDayOneCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','-ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0,r.ScreenPercentage 100"')
        }
        $taskProcesses+=Start-Process -FilePath $taskEditor -WindowStyle Hidden -PassThru -ArgumentList $taskArgs
        if ($taskIndex -eq 0) {
            $taskDeadline=(Get-Date).AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                if ($taskProcesses[0].HasExited) {throw 'Puddle host exited before listening.'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern 'listening on port 7779' -Quiet)
            } until ($taskListening -or (Get-Date) -gt $taskDeadline)
            if (-not $taskListening) {throw 'Puddle host did not start listening.'}
        }
    }
    foreach ($taskProcess in $taskProcesses) {
        if (-not $taskProcess.WaitForExit(120000)) {throw 'Puddle network test timed out.'}
        if ($taskProcess.ExitCode -ne 0) {throw "Puddle process failed: $($taskProcess.ExitCode)"}
    }
    $taskHashes=@()
    foreach ($taskLog in $taskLogs) {
        $taskPass=Select-String -LiteralPath $taskLog -Pattern 'MC_VALIDATION_PASS LIQUID.*masks=(.*)$'
        if (-not $taskPass) {throw "Puddle validation did not pass: $taskLog"}
        $taskHashes+=$taskPass.Matches[0].Groups[1].Value.Trim()
    }
    if (@($taskHashes | Select-Object -Unique).Count -ne 1) {throw 'Host and clients disagree on final wipe masks.'}
    Write-Output "PASS: four processes cleaned four puddles; seeds, sizes, materials and mask checksums agree. $($taskHashes[0])"
} finally {
    foreach ($taskProcess in $taskProcesses) {
        if (-not $taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id}
    }
}
