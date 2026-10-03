param([string]$EngineRoot='E:\UE\UE_5.8',[int]$PacketLagMs=75,[int]$PacketLoss=2,[ValidateRange(1024,65535)][int]$Port=17777,[switch]$Capture,[switch]$Rendered,[ValidatePattern('^[A-Za-z0-9_-]*$')][string]$VideoName='')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskExe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProcesses=@()
$taskPortUsers=@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)
if($taskPortUsers.Count -gt 0) {
    $taskPortOwners=($taskPortUsers | Select-Object -ExpandProperty OwningProcess -Unique) -join ', '
    throw "Dash/suction UDP port $Port is already in use by process IDs $taskPortOwners. Choose another -Port."
}
try {
    for($taskIndex=0;$taskIndex -lt 4;$taskIndex++) {
        $taskMap=if($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{"127.0.0.1:$Port"}
        $taskLog=Join-Path $taskRoot "Saved\Logs\DashSuction$taskIndex.log"
        if(Test-Path -LiteralPath $taskLog) {Remove-Item -LiteralPath $taskLog}
        $taskArgs=@("`"$taskRoot\MessControl.uproject`"",$taskMap,'-game','-nosteam','-MCLegacyDays','-MCDashSuctionTest','-nullrhi','-unattended','-nosound','-nosplash','-nop4',"-PktLag=$PacketLagMs","-PktLoss=$PacketLoss","`"-abslog=$taskLog`"")
        if($taskIndex -eq 0) {$taskArgs+="-port=$Port"}
        if($VideoName) {$taskArgs+='-MCDashSuctionVideo'}
        $taskCommands='t.MaxFPS 60,t.IdleWhenNotForeground 0'
        if(($Capture -or $Rendered -or $VideoName) -and $taskIndex -eq 1) {
            $taskArgs=@($taskArgs | Where-Object {$_ -ne '-nullrhi'})
            $taskArgs+=@('-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages')
            if($Capture) {$taskArgs+='-MCDashSuctionCapture'}
            if($VideoName) {$taskArgs+=@("-MCVideo=$VideoName",'-MCVideoFPS=30','-MCVideoStartSeconds=0')}
            $taskCommands+=',sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,sg.PostProcessQuality 1,r.Streaming.PoolSize 512,r.ScreenPercentage 100'
        }
        $taskArgs+="`"-ExecCmds=$taskCommands`""
        # The Editor target build already verified its SDK. Avoid blocking game
        # startup behind another project's UBT just to repeat platform detection.
        $taskProcesses+=Start-Process -FilePath $taskExe -ArgumentList $taskArgs -PassThru -WindowStyle Hidden -Environment @{UE_SKIP_UBT_SDK_SETUP='1'}
        if($taskIndex -eq 0) {
            $taskDeadline=(Get-Date).AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                if($taskProcesses[0].HasExited) {throw 'Dash/suction host exited before listening.'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern "\blistening on port $Port\b" -Quiet)
            } until($taskListening -or (Get-Date) -gt $taskDeadline)
            if(-not $taskListening) {throw 'Dash/suction host did not start listening.'}
        }
    }
    foreach($taskProcess in $taskProcesses) {
        if(-not $taskProcess.WaitForExit(180000)) {throw 'Dash/suction validation timed out.'}
    }
    for($taskIndex=0;$taskIndex -lt 4;$taskIndex++) {
        $taskLog=Join-Path $taskRoot "Saved\Logs\DashSuction$taskIndex.log"
        $taskFinal=Select-String -LiteralPath $taskLog -Pattern 'MC_VALIDATION_(PASS|FAIL) DASH_SUCTION' | Select-Object -Last 1
        if(-not $taskFinal -or $taskFinal.Line -notmatch 'MC_VALIDATION_PASS') {throw "Dash/suction validation failed: $taskLog"}
        $taskFinal.Line
    }
} finally {
    foreach($taskProcess in $taskProcesses) {if(-not $taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id}}
}
