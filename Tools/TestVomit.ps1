param([string]$EngineRoot='E:\UE\UE_5.8',[switch]$Solo,[switch]$Review,[int]$PacketLagMs=75,[int]$PacketLoss=2)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskExe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskFolder=Join-Path $taskRoot 'Saved\VomitValidation'
New-Item -ItemType Directory -Path $taskFolder -Force | Out-Null
$taskCount=if($Solo){1}else{4}
$taskProcesses=@()
try {
    for($taskIndex=0;$taskIndex -lt $taskCount;$taskIndex++) {
        $taskMap=if($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'127.0.0.1:7777'}
        if($Solo){$taskMap='/Game/Maps/L_Mouth?Seed=41'}
        $taskLog=Join-Path $taskFolder "Network$taskIndex.log"
        $taskArgs=@("`"$taskRoot\MessControl.uproject`"",$taskMap,'-game','-MCLegacyDays','-MCThroatTest',"-MCExpectedPlayers=$taskCount",'-nullrhi','-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"",'"-ExecCmds=t.MaxFPS 60"')
        if(-not $Solo){$taskArgs+=@("-PktLag=$PacketLagMs","-PktLoss=$PacketLoss")}
        if($Review -and $taskIndex -eq 0) {
            $taskArgs=@($taskArgs | Where-Object {$_ -ne '-nullrhi' -and $_ -notlike '*ExecCmds=*'})
            $taskArgs+=@('-MCVomitReview','-RenderOffscreen','-windowed','-ForceRes','-ResX=1440','-ResY=960','-NoScreenMessages','"-ExecCmds=t.MaxFPS 60,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 2,sg.ReflectionQuality 2,sg.ShadowQuality 3,r.ScreenPercentage 100,Trace.Disable Screenshot"')
            if($Solo){$taskArgs+=@('-UseFixedTimeStep','-FPS=30')}
        }
        $taskProcesses+=Start-Process -FilePath $taskExe -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
        if($taskIndex -eq 0 -and -not $Solo) {
            $taskDeadline=(Get-Date).AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                if($taskProcesses[0].HasExited){throw 'Host exited before listening.'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern 'listening on port 7777' -Quiet)
            } until($taskListening -or (Get-Date) -gt $taskDeadline)
            if(-not $taskListening){throw 'Host did not start listening.'}
        }
    }
    foreach($taskProcess in $taskProcesses){if(-not $taskProcess.WaitForExit(180000)){throw 'Vomit validation timed out.'}}
    $taskResults=for($taskIndex=0;$taskIndex -lt $taskCount;$taskIndex++) {
        $taskLog=Join-Path $taskFolder "Network$taskIndex.log"
        $taskMatch=Select-String -LiteralPath $taskLog -Pattern 'MC_VALIDATION_(PASS|FAIL) THROAT' | Select-Object -Last 1
        if(-not $taskMatch -or $taskMatch.Line -notmatch 'MC_VALIDATION_PASS'){throw "Vomit validation failed: $taskLog"}
        $taskMatch.Line
    }
    $taskResults | Set-Content -LiteralPath (Join-Path $taskFolder 'Network.txt') -Encoding utf8
    $taskResults
} finally {
    foreach($taskProcess in $taskProcesses){if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
}
