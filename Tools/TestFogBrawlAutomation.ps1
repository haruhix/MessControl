param(
    [string]$EngineRoot = 'E:\UE\UE_5.8',
    [string]$OutputDirectory = '',
    [ValidateRange(30,1800)][int]$TimeoutSeconds = 600,
    [ValidateSet('MessControl.FogBrawl','MessControl.SingleDay','MessControl.Grip.Brace','MessControl.Development','MessControl.IceEvent','MessControl.Inventory','MessControl.Camera')]
    [string[]]$Suites = @('MessControl.FogBrawl','MessControl.SingleDay','MessControl.Grip.Brace','MessControl.Development','MessControl.IceEvent','MessControl.Inventory','MessControl.Camera')
)
$ErrorActionPreference = 'Stop'
$taskProjectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskProject = Join-Path $taskProjectRoot 'MessControl.uproject'
$taskEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskProjectRoot 'Saved\FogBrawlValidation\automation' }
$taskRunFolder = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) (Get-Date -Format 'yyyyMMdd_HHmmss_fff')
New-Item -ItemType Directory -Path $taskRunFolder -Force | Out-Null
$taskReportFolder = Join-Path $taskRunFolder 'report'
$taskIndex = Join-Path $taskReportFolder 'index.json'
$taskLog = Join-Path $taskRunFolder 'Automation.log'

# Fixed manifest detects stale binaries, missing tests and accidental over-selection.
# Inventory focuses tool routing/presentation after character, input and camera changes.
$taskManifest = [ordered]@{
    'MessControl.FogBrawl' = @(
        'GenericRMBProtectsReserveAndResolvesOnce','TeamSharesLoadAndLoneDefenderTakesFullLoad',
        'MissUsesNormalToothLossAndConsumedTargetRestartsWarning','DefenceRequiresNearGroundedLivingCrewAndClearApproach',
        'CompleteCancelReplaceAndMissingReserve','ClientCannotStartOrResolveStrikes','WaitsForNormalGetupAndStopsAfterShiftTerminal'
    )
    'MessControl.SingleDay' = @(
        'TutorialExperiencePreservesPawnAndChoices','LegacyFinalBossDeathEndsRun','FailedNutSetupTerminatesRun',
        'Sequence.CompletedFogSlotOpensSupportWithoutFinalBoss','Sequence.SupportSurvivesOldFinaleDayAndRunTargets',
        'Sequence.NutIntervalDispatchesIceAndStopsSupport','Sequence.SavedNutOnlyProfileAppendsIceAndFogWithoutChangingSavedSettings',
        'Sequence.IceIntervalDispatchesFogAndStopsSupport','Sequence.SavedNutIceProfileAppendsFogWithoutChangingSavedSettings',
        'Sequence.FailedFogSetupTerminatesAndCleansEvent','Sequence.FailedIceSetupTerminatesAndCleansEvent',
        'Sequence.SupportAdmitsFoodColaAndRepairWithoutDayOrDeadlineGates','Sequence.SharedHistoryIsBoundedAndSurvivesKeyEventPublish'
    )
    'MessControl.Grip.Brace' = @('ContactChain','RiverAndReplay','DynamicFoodLoad')
    'MessControl.Development' = @('EventSandbox')
    'MessControl.IceEvent' = @(
        'MeterWarmsCoolsAndKillsAtFull','IcicleWarningStaysFixedAndAllowsDodge','IcicleDamagesMarkedAreaOnlyOnce',
        'CancelReplaceAndCompleteRestoreArena','ClientCannotStartOrApplyGameplay','MissingTongueFailsWithoutLeakingGameplay'
    )
    'MessControl.Inventory' = @('SelectionAndMaterial','PermanentToolsNeverDrop','HiddenPickaxePreservesHandContacts','HoldSprayAndPreserveProgress')
    'MessControl.Camera' = @(
        'BlueprintDefaultsReachPlayerView','OrbitAndWallCollision','FoodTransparency',
        'CharacterAndToothTransparency','MovementAndCollisionStability','RadialWallReveal'
    )
}
$taskSuites = @($Suites | Select-Object -Unique)
if ($taskSuites.Count -eq 0) { throw 'Select at least one focused suite.' }
$taskExpected = @($taskSuites | ForEach-Object {
    $taskSuite = $_
    $taskManifest[$taskSuite] | ForEach-Object { "$taskSuite.$_" }
})

function ConvertTo-FogNativeArgument([string]$Value) {
    # Start-Process joins ArgumentList on Windows; preserve boundaries with CRT quoting.
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    return '"' + ($Value -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

# UE 5.8 AutomationCommandline.cpp splits selectors on '+' and accepts one queued RunTests.
# Semicolons delimit SetFilter/RunTests/Quit inside the single Automation command.
$taskExec = 'Automation SetFilter Engine; RunTests ' + ($taskExpected -join '+') + '; Quit'
$taskRawArguments = @($taskProject,'-unattended','-nop4','-nosplash','-nosound','-nosteam','-nullrhi',
    "-ExecCmds=$taskExec",'-TestExit=Automation Test Queue Empty',"-ReportExportPath=$taskReportFolder","-abslog=$taskLog")
$taskArguments = ($taskRawArguments | ForEach-Object { ConvertTo-FogNativeArgument $_ }) -join ' '
$taskProcess = $null
$taskExitCode = $null
$taskFailure = $null
$taskReport = $null
$taskWarningEvents = @()
$taskErrorEvents = @()
$taskErrors = [Collections.Generic.List[string]]::new()
$taskSuiteResults = @()
$taskStartedAt = Get-Date
$taskTimedOut = $false

try {
    [pscustomobject]@{editor=$taskEditor;project=$taskProject;suites=$taskSuites;expectedTests=$taskExpected;arguments=$taskRawArguments;timeoutSeconds=$TimeoutSeconds} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Invocation.json') -Encoding utf8
    if (-not (Test-Path -LiteralPath $taskEditor)) { throw "Editor executable missing: $taskEditor" }
    if (-not (Test-Path -LiteralPath $taskProject)) { throw "Project missing: $taskProject" }
    $taskProcess = Start-Process -FilePath $taskEditor -ArgumentList $taskArguments -WindowStyle Hidden -PassThru
    Write-Output "START pid=$($taskProcess.Id) tests=$($taskExpected.Count) reports=$taskRunFolder"
    $taskDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $taskProcess.HasExited) {
        if ((Get-Date) -ge $taskDeadline) { $taskTimedOut = $true; throw "Owned Unreal process $($taskProcess.Id) timed out: $taskRunFolder" }
        Start-Sleep -Milliseconds 500
        $taskProcess.Refresh()
    }
    $taskProcess.WaitForExit()
    $taskExitCode = $taskProcess.ExitCode
    if (-not (Test-Path -LiteralPath $taskIndex)) { throw "Automation report missing (exit $taskExitCode): $taskIndex" }
    $taskReport = Get-Content -Raw -LiteralPath $taskIndex | ConvertFrom-Json
    foreach ($taskProperty in @('succeeded','succeededWithWarnings','failed','notRun','inProcess','tests')) {
        if ($taskProperty -notin $taskReport.PSObject.Properties.Name) { throw "Automation JSON is missing required field '$taskProperty': $taskIndex" }
    }
    foreach ($taskProperty in @('succeeded','succeededWithWarnings','failed','notRun','inProcess')) {
        $taskCount = $taskReport.$taskProperty
        if ($taskCount -isnot [int] -and $taskCount -isnot [long]) { throw "Automation JSON counter '$taskProperty' must be an integer." }
        if ($taskCount -lt 0) { throw "Automation JSON counter '$taskProperty' must be nonnegative." }
    }
    if ($taskExitCode -ne 0) { $taskErrors.Add("Editor exit code is $taskExitCode.") }
    if ($taskReport.failed -ne 0 -or $taskReport.notRun -ne 0 -or $taskReport.inProcess -ne 0) {
        $taskErrors.Add("Report has failed=$($taskReport.failed), notRun=$($taskReport.notRun), inProcess=$($taskReport.inProcess).")
    }
    $taskTests = @($taskReport.tests)
    if (($taskReport.succeeded + $taskReport.succeededWithWarnings) -ne $taskExpected.Count -or $taskTests.Count -ne $taskExpected.Count) {
        $taskErrors.Add("Expected exactly $($taskExpected.Count) successful test records; report contains $($taskTests.Count).")
    }
    foreach ($taskName in $taskExpected) {
        $taskMatches = @($taskTests | Where-Object fullTestPath -eq $taskName)
        if ($taskMatches.Count -ne 1 -or $taskMatches[0].state -notin @('Success','SuccessWithWarnings') -or $taskMatches[0].errors -ne 0) {
            $taskErrors.Add("Expected test missing, duplicated or unsuccessful: $taskName")
        }
    }
    foreach ($taskTest in $taskTests) {
        foreach ($taskProperty in @('fullTestPath','state','warnings','errors','entries')) {
            if ($taskProperty -notin $taskTest.PSObject.Properties.Name) { throw "Automation test record is missing '$taskProperty'." }
        }
        if ($taskTest.fullTestPath -notin $taskExpected) { $taskErrors.Add("Unexpected test selected: $($taskTest.fullTestPath)") }
        foreach ($taskProperty in @('warnings','errors')) {
            $taskCount = $taskTest.$taskProperty
            if (($taskCount -isnot [int] -and $taskCount -isnot [long]) -or $taskCount -lt 0) { throw "Invalid $taskProperty counter: $($taskTest.fullTestPath)" }
        }
        foreach ($taskEntry in @($taskTest.entries)) {
            if ($taskEntry.event.type -eq 'Warning') {
                $taskWarningEvents += [pscustomobject]@{test=$taskTest.fullTestPath;message=$taskEntry.event.message;file=$taskEntry.filename;line=$taskEntry.lineNumber;timestamp=$taskEntry.timestamp}
            }
            elseif ($taskEntry.event.type -eq 'Error') {
                $taskErrorEvents += [pscustomobject]@{test=$taskTest.fullTestPath;message=$taskEntry.event.message;file=$taskEntry.filename;line=$taskEntry.lineNumber;timestamp=$taskEntry.timestamp}
            }
        }
        if (@($taskTest.entries | Where-Object { $_.event.type -eq 'Warning' }).Count -ne $taskTest.warnings -or
            @($taskTest.entries | Where-Object { $_.event.type -eq 'Error' }).Count -ne $taskTest.errors) {
            $taskErrors.Add("Test entry counts differ from warnings/errors counters: $($taskTest.fullTestPath)")
        }
    }
    $taskSuccessful = @($taskTests | Where-Object { $_.state -in @('Success','SuccessWithWarnings') -and $_.errors -eq 0 })
    if (@($taskSuccessful | Where-Object warnings -eq 0).Count -ne $taskReport.succeeded -or
        @($taskSuccessful | Where-Object warnings -gt 0).Count -ne $taskReport.succeededWithWarnings) {
        $taskErrors.Add('Successful record warning counts differ from the report totals.')
    }
    foreach ($taskSuite in $taskSuites) {
        $taskNames = @($taskManifest[$taskSuite] | ForEach-Object { "$taskSuite.$_" })
        $taskCases = @($taskTests | Where-Object { $_.fullTestPath -in $taskNames })
        $taskSuiteResults += [pscustomobject]@{
            suite=$taskSuite;expected=$taskNames.Count;reported=$taskCases.Count
            passed=@($taskCases | Where-Object { $_.state -in @('Success','SuccessWithWarnings') -and $_.errors -eq 0 }).Count
            warningEntries=@($taskWarningEvents | Where-Object { $_.test -in $taskNames }).Count
            tests=@($taskCases | Select-Object fullTestPath,state,warnings,errors)
        }
    }
    if ($taskErrors.Count -gt 0) { throw ($taskErrors -join ' ') }
} catch {
    $taskFailure = $_.Exception.Message
    if ($taskErrors.Count -eq 0) { $taskErrors.Add($taskFailure) }
} finally {
    if ($taskProcess) {
        try {
            if (-not $taskProcess.HasExited) {
                $taskProcess.Kill()
                if (-not $taskProcess.WaitForExit(5000)) { throw "Owned Unreal process $($taskProcess.Id) did not exit after termination." }
            }
            if ($taskProcess.HasExited) { $taskExitCode = $taskProcess.ExitCode }
        } catch {
            $taskFailure = "Owned process cleanup failed: $($_.Exception.Message)"
            $taskErrors.Add($taskFailure)
        } finally { $taskProcess.Dispose() }
    }
    $taskStatus = if ($taskFailure) { 'FAIL' } elseif ($taskWarningEvents.Count -gt 0) { 'PASS_WITH_WARNINGS' } else { 'PASS' }
    $taskSummary = [pscustomobject]@{
        status=$taskStatus;exitCode=$taskExitCode;timedOut=$taskTimedOut;expectedCount=$taskExpected.Count
        succeeded=if($taskReport){$taskReport.succeeded}else{$null}
        succeededWithWarnings=if($taskReport){$taskReport.succeededWithWarnings}else{$null}
        failed=if($taskReport){$taskReport.failed}else{$null};notRun=if($taskReport){$taskReport.notRun}else{$null}
        inProcess=if($taskReport){$taskReport.inProcess}else{$null};warningEntryCount=$taskWarningEvents.Count
        startedAt=$taskStartedAt.ToString('o');finishedAt=(Get-Date).ToString('o')
        report=$taskIndex;log=$taskLog;errors=@($taskErrors);suites=$taskSuiteResults;warnings=$taskWarningEvents;testErrors=$taskErrorEvents
        scope='Editor -nullrhi focused automation; does not validate rendered smoke, live networking or cooked packages. Warnings are retained without suppression.'
    }
    $taskSummary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.json') -Encoding utf8
    @("$taskStatus expected=$($taskExpected.Count) exit=$taskExitCode warningEntries=$($taskWarningEvents.Count)","Report: $taskIndex","Log: $taskLog",$taskSummary.scope) |
        Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.txt') -Encoding utf8
}
if ($taskFailure) { throw "$taskFailure See $taskRunFolder\Validation.json" }
Write-Output "$taskStatus tests=$($taskExpected.Count) warningEntries=$($taskWarningEvents.Count) report=$taskIndex"
