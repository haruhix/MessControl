param(
    [string]$EngineRoot = 'E:\UE\UE_5.8',
    [ValidateSet('Unit','Network','Visual')][string]$Mode = 'Unit',
    [string]$OutputDirectory = 'E:\DEVGAME\MessControl-DirectorLab\results\ue_director'
)
$ErrorActionPreference = 'Stop'
$taskProjectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskProject = Join-Path $taskProjectRoot 'MessControl.uproject'
$taskEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $taskEditor)) { throw "Editor executable not found: $taskEditor" }
if (-not (Test-Path -LiteralPath $taskProject)) { throw "Project not found: $taskProject" }
$taskRunFolder = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ("{0}_{1}" -f $Mode,(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
New-Item -ItemType Directory -Path $taskRunFolder -Force | Out-Null
$taskOwnedProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()

function Start-DirectorProcess([string[]]$Arguments) {
    $taskStarted = Start-Process -FilePath $taskEditor -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    $taskOwnedProcesses.Add($taskStarted)
    return $taskStarted
}
function Wait-DirectorProcess([Diagnostics.Process]$Process,[int]$Seconds) {
    $taskDeadline = (Get-Date).AddSeconds($Seconds)
    while (-not $Process.HasExited) {
        if ((Get-Date) -ge $taskDeadline) { throw "Owned Unreal process $($Process.Id) timed out; reports: $taskRunFolder" }
        Start-Sleep -Milliseconds 500
        $Process.Refresh()
    }
    $Process.WaitForExit()
    if ($Process.ExitCode -ne 0) { throw "Owned Unreal process $($Process.Id) exited with code $($Process.ExitCode); reports: $taskRunFolder" }
}
function Assert-DirectorSmokeLog([string]$Log,[string]$Role) {
    if (-not (Test-Path -LiteralPath $Log)) { throw "Missing runtime log: $Log" }
    $taskLines = Get-Content -LiteralPath $Log
    if ($taskLines -match '\bMC_DIRECTOR_(?:SMOKE_FAIL|CHECK FAIL)\b') { throw "Director smoke reported failure: $Log" }
    if ($taskLines -match 'FNetGUIDCache::SupportsObject: MCDayPlan') { throw "DayPlan uses an unsupported transient network object: $Log" }
    $taskPass = @($taskLines | Where-Object { $_ -match "\bMC_DIRECTOR_SMOKE_PASS role=$Role\b" })
    if ($taskPass.Count -ne 1) { throw "Expected exactly one Director smoke pass for $Role`: $Log" }
    if ($Role -eq 'client' -and $taskPass[0] -notmatch '\bremote_seen=511\b') { throw "Remote client did not observe the entire replicated adaptive protocol: $Log" }
    return $taskPass[0]
}

try {
    if ($Mode -eq 'Unit') {
        $taskReportFolder = Join-Path $taskRunFolder 'automation'
        New-Item -ItemType Directory -Path $taskReportFolder -Force | Out-Null
        $taskLog = Join-Path $taskRunFolder 'Automation.log'
        $taskArgs = @("`"$taskProject`"",'-unattended','-nop4','-nosplash','-nosound','-nullrhi',
            '"-ExecCmds=Automation SetFilter Engine; RunTests MessControl.Director; Quit"',
            '"-TestExit=Automation Test Queue Empty"',"`"-ReportExportPath=$taskReportFolder`"","`"-abslog=$taskLog`"")
        $taskProcess = Start-DirectorProcess $taskArgs
        Wait-DirectorProcess $taskProcess 300
        $taskIndex = Join-Path $taskReportFolder 'index.json'
        if (-not (Test-Path -LiteralPath $taskIndex)) { throw "Automation did not export a report: $taskIndex" }
        $taskReport = Get-Content -Raw -LiteralPath $taskIndex | ConvertFrom-Json
        $taskExpected = @(
            'SevenDaysAndFiniteProfileBounds','RunSnapshotsSettingsAndRuntimeObservation','OneFragmentAndQueuedMealBlockCompletion',
            'AcceptedPepperCanFinishRealSwallow','ExistingYawnReservesGlobalChannel','MonitorStateIsReplicatedAsOneProperty',
            'AutomaticSevenDayLifecycle','DeadlineDoesNotPenalizeUnselectedEvents','ReinitializeRetiresOldServices',
            'IntakeDuringWarningCommitsWithoutDeadlock','GlobalProducersRequireDirectorGrant','DisposedPepperAftermathRemainsWork',
            'RuntimeSelectionsReproduceFromSameSeed','RuntimeCandidatesRespondToObservedWorld',
            'RuntimeCooldownAndDiversityAffectChoices','AdaptationRelievesStrugglingTeam',
            'FoodForecastBoundsActualAdmission','SelectedColaLimitsIceWithoutEditingSavedProfile',
            'TripleDifficultyScalesSevenActiveDaysWithoutChangingAssets',
            'OrdinaryFoodDoesNotStarveCoffeeWhileSpecialGatesRemain'
        )
        $taskTests = @($taskReport.tests | Where-Object { $_.fullTestPath -like 'MessControl.Director.*' })
        if ($taskReport.failed -ne 0 -or ($taskReport.succeeded + $taskReport.succeededWithWarnings) -ne $taskExpected.Count -or
            $taskTests.Count -ne $taskExpected.Count -or $taskReport.notRun -ne 0 -or $taskReport.inProcess -ne 0) {
            throw "Expected all $($taskExpected.Count) focused Director tests to complete successfully: $taskIndex"
        }
        foreach ($taskName in $taskExpected) {
            $taskMatches = @($taskTests | Where-Object { $_.fullTestPath -eq "MessControl.Director.$taskName" })
            if ($taskMatches.Count -ne 1 -or $taskMatches[0].state -notin @('Success','SuccessWithWarnings')) {
                throw "Focused test missing or unsuccessful: $taskName; report: $taskIndex"
            }
        }
        $taskMessage = "PASS: all $($taskExpected.Count) Director automation tests completed. Report: $taskIndex"
        $taskMessage | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.txt') -Encoding utf8
        Write-Output $taskMessage
    } elseif ($Mode -eq 'Network') {
        $taskPort = 7783
        $taskLogs = [Collections.Generic.List[string]]::new()
        for ($taskPeer = 0; $taskPeer -lt 4; ++$taskPeer) {
            $taskMap = if ($taskPeer -eq 0) { '/Game/Maps/L_Mouth?listen?Seed=41' } else { "127.0.0.1:$taskPort" }
            $taskLog = Join-Path $taskRunFolder "Network$taskPeer.log"
            $taskLogs.Add($taskLog)
            $taskArgs = @("`"$taskProject`"",$taskMap,'-game','-nosteam',"-port=$taskPort",'-MCGameDirectorTest',
                '-MCGameDirectorExpectedPlayers=4','-nullrhi','-unattended','-nosound','-nosplash','-nop4',
                '"-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0"',"`"-abslog=$taskLog`"",
                '-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=30')
            $taskHost = Start-DirectorProcess $taskArgs
            if ($taskPeer -eq 0) {
                $taskListenDeadline = (Get-Date).AddSeconds(90)
                $taskListening = $false
                while (-not $taskListening) {
                    if ($taskHost.HasExited) { throw "Host exited before listening: $taskLog" }
                    if ((Get-Date) -ge $taskListenDeadline) { throw "Host did not listen on port $taskPort`: $taskLog" }
                    Start-Sleep -Milliseconds 500
                    $taskListening = (Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern "listening on port $taskPort\b" -Quiet)
                    $taskHost.Refresh()
                }
            }
        }
        foreach ($taskProcess in $taskOwnedProcesses) { Wait-DirectorProcess $taskProcess 240 }
        $taskMarkers = for ($taskPeer = 0; $taskPeer -lt 4; ++$taskPeer) {
            Assert-DirectorSmokeLog $taskLogs[$taskPeer] $(if ($taskPeer -eq 0) { 'authority' } else { 'client' })
        }
        $taskMarkers | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.txt') -Encoding utf8
        Write-Output "PASS: saved L_Mouth authority plus three remote clients verified the Director food/monitor protocol. Reports: $taskRunFolder"
    } else {
        $taskLog = Join-Path $taskRunFolder 'Visual.log'
        $taskCaptureFolder = Join-Path $taskProjectRoot 'Saved\DirectorValidation'
        $taskExistingCaptures = @('DirectorProduction.png','DirectorMonitor.png','DirectorComplete.png')
        # Remove only these explicitly owned capture targets to prevent stale-image passes.
        foreach ($taskName in $taskExistingCaptures) {
            $taskCapture = Join-Path $taskCaptureFolder $taskName
            if (Test-Path -LiteralPath $taskCapture) { Remove-Item -LiteralPath $taskCapture }
        }
        $taskStartTime = Get-Date
        $taskArgs = @("`"$taskProject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-nosteam','-MCGameDirectorTest',
            '-MCGameDirectorExpectedPlayers=1','-MCGameDirectorCapture','-RenderOffscreen','-windowed','-ForceRes',
            '-ResX=1600','-ResY=1000','-NoScreenMessages','-unattended','-nosound','-nosplash','-nop4',
            '"-ExecCmds=MC.Director.Debug 1,t.MaxFPS 30,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,r.ScreenPercentage 100"',
            "`"-abslog=$taskLog`"",'-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=30')
        $taskProcess = Start-DirectorProcess $taskArgs
        Wait-DirectorProcess $taskProcess 300
        $taskMarker = Assert-DirectorSmokeLog $taskLog 'authority'
        foreach ($taskName in $taskExistingCaptures) {
            $taskCapture = Join-Path $taskCaptureFolder $taskName
            if (-not (Test-Path -LiteralPath $taskCapture)) { throw "Rendered screenshot missing: $taskCapture" }
            $taskImage = Get-Item -LiteralPath $taskCapture
            if ($taskImage.Length -lt 1024 -or $taskImage.LastWriteTime -lt $taskStartTime) { throw "Screenshot is stale or empty: $taskCapture" }
            $taskBytes = [IO.File]::ReadAllBytes($taskCapture)
            if ([BitConverter]::ToString($taskBytes[0..7]) -ne '89-50-4E-47-0D-0A-1A-0A') { throw "Capture is not a PNG: $taskCapture" }
            $taskWidth = [BitConverter]::ToUInt32([byte[]]@($taskBytes[19],$taskBytes[18],$taskBytes[17],$taskBytes[16]),0)
            $taskHeight = [BitConverter]::ToUInt32([byte[]]@($taskBytes[23],$taskBytes[22],$taskBytes[21],$taskBytes[20]),0)
            if ($taskWidth -ne 1600 -or $taskHeight -ne 1000) { throw "Unexpected capture dimensions ${taskWidth}x${taskHeight}: $taskCapture" }
            Copy-Item -LiteralPath $taskCapture -Destination (Join-Path $taskRunFolder $taskName)
        }
        $taskMarker | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.txt') -Encoding utf8
        Write-Output "PASS: production profile and saved-arena protocol rendered three current HUD screenshots; visual inspection remains required. Reports: $taskRunFolder"
    }
} finally {
    # Never enumerate or terminate unrelated editor/game processes.
    foreach ($taskProcess in $taskOwnedProcesses) {
        $taskProcess.Refresh()
        if (-not $taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id }
    }
}
