param([string]$EngineRoot=$env:UE_ROOT,[ValidateSet('Unit','Network','CoreNetwork','DayOneNetwork','DevPanelNetwork','CoffeeNetwork','TongueNetwork','TongueJoltNetwork','ArenaNetwork','Visual','Ragdoll','RagdollVisual','Limbs')][string]$Mode='Unit',[ValidateRange(0,250)][int]$PacketLagMs=0,[ValidateRange(0,10)][int]$PacketLoss=0,[ValidateSet(30,60,120)][int]$FrameRate=60,[switch]$CaptureCore,[switch]$CaptureDayOne,[switch]$CaptureDevPanel,[switch]$CaptureCoffee,[switch]$CaptureTongue)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProject=Join-Path $taskRoot 'MessControl.uproject'
$taskLogs=Join-Path $taskRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $taskLogs -Force | Out-Null
if ($Mode -eq 'Unit') {
    & $taskEditor $taskProject -unattended -nop4 -nosplash -nullrhi '-ExecCmds=Automation RunTests MessControl; Quit' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$taskRoot\Saved\TestReports" "-abslog=$taskLogs\Automation.log"
    if ($LASTEXITCODE -ne 0) { throw 'Unreal automation failed.' }
    $taskReport=Get-Content -Raw "$taskRoot\Saved\TestReports\index.json" | ConvertFrom-Json
    if ($taskReport.failed -ne 0 -or ($taskReport.succeeded + $taskReport.succeededWithWarnings) -lt 23) { throw 'Not all gameplay and physics tests passed.' }
} elseif ($Mode -eq 'Limbs') {
    & $taskEditor $taskProject '/Game/Maps/L_Mouth?Seed=41' -game -MCLegacyDays -MCLimbs -nullrhi -unattended -nosound -nop4 "-ExecCmds=t.MaxFPS $FrameRate" "-abslog=$taskLogs\Limbs$FrameRate.log"
    if ($LASTEXITCODE -ne 0 -or -not (Select-String -Path "$taskLogs\Limbs$FrameRate.log" -Pattern 'MC_LIMBS_PASS' -Quiet)) { throw 'Limb stability regression. See Limbs log.' }
} elseif ($Mode -eq 'Visual' -or $Mode -eq 'RagdollVisual') {
    $taskCapture=if($Mode -eq 'RagdollVisual'){'-MCRagdollCapture'}else{'-MCCapture'}
    & $taskEditor $taskProject '/Game/Maps/L_Mouth?Seed=41' -game -MCLegacyDays $taskCapture -RenderOffscreen -windowed -ForceRes -ResX=1440 -ResY=960 -unattended -nosound -nosplash -nop4 "-abslog=$taskLogs\$Mode.log"
    if ($LASTEXITCODE -ne 0 -or -not (Select-String -Path "$taskLogs\$Mode.log" -Pattern 'MC_VALIDATION_PASS' -Quiet)) { throw 'Visual smoke run failed.' }
} else {
    $taskProcesses=@()
    try {
        for ($taskIndex=0; $taskIndex -lt 4; $taskIndex++) {
            $taskMap=if($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'127.0.0.1:7777'}
            $taskLog=Join-Path $taskLogs "$Mode$taskIndex.log"
            if (Test-Path -LiteralPath $taskLog) { Remove-Item -LiteralPath $taskLog }
            $taskArguments=@("`"$taskProject`"",$taskMap,'-game','-MCLegacyDays','-MCSmoke','-MCExpectedPlayers=4','-nullrhi','-unattended','-nosound','-nosplash','-nop4','-ExecCmds="t.MaxFPS 60"',"`"-abslog=$taskLog`"")
            if($Mode -eq 'Ragdoll') { $taskArguments+='-MCRagdoll' }
            if($Mode -eq 'ArenaNetwork') { $taskArguments+='-MCArenaNet' }
            if($Mode -eq 'CoreNetwork') { $taskArguments+='-MCCore' }
            if($Mode -eq 'DayOneNetwork') { $taskArguments+='-MCDayOne' }
            if($Mode -eq 'DevPanelNetwork') { $taskArguments+='-MCDevPanelSmoke' }
            if($Mode -eq 'CoffeeNetwork') { $taskArguments+='-MCCoffeeWaterTest' }
            if($Mode -in @('TongueNetwork','TongueJoltNetwork')) { $taskArguments+='-MCTongueTest' }
            if($Mode -eq 'TongueJoltNetwork') { $taskArguments+='-MCTongueJolt' }
            if($CaptureTongue -and $Mode -in @('TongueNetwork','TongueJoltNetwork') -and $taskIndex -eq 0) {
                $taskArguments=@($taskArguments | Where-Object { $_ -ne '-nullrhi' -and $_ -notlike '-ExecCmds=*' })
                $taskArguments+=@('-MCTongueCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','-ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0"')
            }
            if($CaptureCoffee -and $Mode -eq 'CoffeeNetwork' -and $taskIndex -eq 0) {
                $taskArguments=@($taskArguments | Where-Object { $_ -ne '-nullrhi' -and $_ -notlike '-ExecCmds=*' })
                $taskArguments+=@('-MCCoffeeWaterCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','-ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0"')
            }
            if($CaptureDevPanel -and $Mode -eq 'DevPanelNetwork' -and $taskIndex -eq 0) {
                $taskArguments=@($taskArguments | Where-Object { $_ -ne '-nullrhi' })
                $taskArguments+=@('-MCDevPanelCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1440','-ResY=900','-NoScreenMessages')
            }
            if($CaptureDayOne -and $Mode -eq 'DayOneNetwork' -and $taskIndex -eq 0) {
                $taskArguments=@($taskArguments | Where-Object { $_ -ne '-nullrhi' })
                $taskArguments+=@('-MCDayOneCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages')
            }
            if($CaptureCore -and $Mode -eq 'CoreNetwork' -and $taskIndex -eq 0) {
                $taskArguments=@($taskArguments | Where-Object { $_ -ne '-nullrhi' })
                $taskArguments+=@('-MCCoreCapture','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages')
            }
            if($PacketLagMs -gt 0) { $taskArguments+="-PktLag=$PacketLagMs" }
            if($PacketLoss -gt 0) { $taskArguments+="-PktLoss=$PacketLoss" }
            $taskProcesses += Start-Process -FilePath $taskEditor -WindowStyle Hidden -PassThru -ArgumentList $taskArguments
            if ($taskIndex -eq 0) {
                $taskDeadline=(Get-Date).AddSeconds(60)
                do {
                    Start-Sleep -Milliseconds 500
                    if ($taskProcesses[0].HasExited) { throw 'Host exited before listening.' }
                    $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -Path $taskLog -Pattern 'listening on port 7777' -Quiet)
                } until ($taskListening -or (Get-Date) -gt $taskDeadline)
                if (-not $taskListening) { throw 'Host did not start listening in time.' }
            }
        }
        foreach ($taskProcess in $taskProcesses) {
            if (-not $taskProcess.WaitForExit(150000)) { throw 'Network test timed out.' }
        }
        for ($taskIndex=0;$taskIndex -lt 4;$taskIndex++) {
            if (-not (Select-String -Path "$taskLogs\$Mode$taskIndex.log" -Pattern 'MC_VALIDATION_PASS' -Quiet)) { throw "Client $taskIndex failed $Mode validation. See its log." }
        }
        Write-Output 'PASS: four processes connected and observed authoritative task progress.'
    } finally {
        foreach($taskProcess in $taskProcesses) { if (-not $taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id } }
    }
}
