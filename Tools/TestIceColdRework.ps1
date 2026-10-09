param(
    [string]$EngineRoot = 'E:\UE\UE_5.8',
    [ValidateSet('Runtime','Network','Visual')][string]$Mode = 'Runtime',
    [ValidateRange(1,2)][int]$Players = 1,
    [switch]$RequireVFX,
    [string]$OutputDirectory = '',
    [ValidateRange(1024,65535)][int]$Port = 7798,
    [ValidateRange(0,1800)][int]$TimeoutSeconds = 0
)
$ErrorActionPreference = 'Stop'
if ($TimeoutSeconds -eq 0) { $TimeoutSeconds = if ($Mode -eq 'Visual') { 1000 } else { 500 } }
$taskProjectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskProject = Join-Path $taskProjectRoot 'MessControl.uproject'
$taskEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $taskEditor)) { throw "Editor executable missing: $taskEditor" }
if (-not (Test-Path -LiteralPath $taskProject)) { throw "Project missing: $taskProject" }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskProjectRoot 'Saved\IceColdReworkValidation' }
$taskRunFolder = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ("{0}_{1}" -f $Mode,(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
New-Item -ItemType Directory -Path $taskRunFolder -Force | Out-Null
$taskCapture = Join-Path $taskRunFolder 'Frames'
$taskPeers = if ($Mode -eq 'Network') { 2 } else { $Players }
$taskOwnedProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()
$taskInvocations = [Collections.Generic.List[object]]::new()
$taskLogs = [Collections.Generic.List[string]]::new()
$taskResults = @()
$taskFailure = $null
$taskFrameCount = 0
$taskStartedAt = Get-Date
$taskDeadline = $taskStartedAt.AddSeconds($TimeoutSeconds)

function ConvertTo-ColdArgument([string]$Value) {
    # Start-Process joins ArgumentList; quote arguments using the Windows CRT rules.
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    return '"' + ($Value -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

function Start-ColdProcess([string[]]$Arguments) {
    $taskArgumentString = ($Arguments | ForEach-Object { ConvertTo-ColdArgument $_ }) -join ' '
    $taskStarted = Start-Process -FilePath $taskEditor -ArgumentList $taskArgumentString -WindowStyle Hidden -PassThru
    $taskOwnedProcesses.Add($taskStarted)
    $taskInvocations.Add([pscustomobject]@{pid=$taskStarted.Id;arguments=$Arguments})
    return $taskStarted
}

function Assert-NoColdFailure {
    foreach ($taskExistingLog in $taskLogs) {
        if (-not (Test-Path -LiteralPath $taskExistingLog)) { continue }
        $taskFailureLine = Select-String -LiteralPath $taskExistingLog -Pattern 'MC_ICE_COLD_FAIL|Assertion failed:|Fatal error:|Ensure condition failed:' |
            Select-Object -Last 1
        if ($taskFailureLine) { throw "Runtime failure in $taskExistingLog`: $($taskFailureLine.Line.Trim())" }
    }
}

function Assert-ColdLog([string]$Log,[string]$Role) {
    if (-not (Test-Path -LiteralPath $Log)) { throw "Missing runtime log: $Log" }
    $taskLines = Get-Content -LiteralPath $Log
    if ($taskLines -match 'MC_ICE_COLD_FAIL|Assertion failed:|Fatal error:|Ensure condition failed:') { throw "Runtime failure in $Log" }
    $taskPass = @($taskLines | Where-Object { $_ -match "MC_ICE_COLD_PASS role=$Role\b" })
    if ($taskPass.Count -ne 1) { throw "Expected exactly one focused cold pass for $Role`: $Log" }
    if ($RequireVFX -and $taskPass[0] -notmatch 'vfx=1\b') { throw "Missing authored VFX lifecycle evidence: $Log" }
    if ($Role -eq 'client') {
        if ($RequireVFX -and $taskPass[0] -notmatch 'completion_vfx=1\b') { throw "Remote success cue was not observed: $Log" }
        if ($taskPass[0] -notmatch 'remote_seen=8191\b' -or $taskPass[0] -notmatch 'native_lmb_presses=[1-9]\d*\b') {
            throw "Remote peer missed replicated cold state or its own native foot rescue: $Log"
        }
    } else {
        foreach ($taskToken in @('zones=45,40,35,30,25,20','overlap=10','series=3','cooldown=12','blocker_native=1',
            'cone_native=1','self_axe_native=1','dodge=1','hit=1','complete_native=1','cancel_pending=1')) {
            if ($taskPass[0] -notmatch ([regex]::Escape($taskToken)+'\b')) { throw "Authority missed '$taskToken': $Log" }
        }
        foreach ($taskZone in 0..5) {
            if (@($taskLines | Where-Object { $_ -match "MC_ICE_COLD_CHECK PASS zone=$taskZone\b" }).Count -ne 1) {
                throw "Actual production zone $taskZone was missing or checked twice: $Log"
            }
        }
        foreach ($taskCheck in @('f3_start=1','series=3','both_warm=1','warming_resumed=1','self_native_hits=',
            'fixed_dodge=1','stationary_hit=','native_core_complete=1')) {
            if (-not ($taskLines | Where-Object { $_ -match 'MC_ICE_COLD_CHECK PASS' -and $_ -match [regex]::Escape($taskCheck) })) {
                throw "Missing actual runtime evidence '$taskCheck': $Log"
            }
        }
    }
    return $taskPass[0]
}

try {
    if ($taskPeers -gt 1 -and (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)) {
        throw "UDP port $Port is already occupied; choose another -Port"
    }
    for ($taskPeer = 0; $taskPeer -lt $taskPeers; ++$taskPeer) {
        $taskMap = if ($taskPeer -eq 0) {
            if ($taskPeers -gt 1) { '/Game/Maps/L_Mouth?listen?Seed=41' } else { '/Game/Maps/L_Mouth?Seed=41' }
        } else { "127.0.0.1:$Port" }
        $taskLog = Join-Path $taskRunFolder "Peer$taskPeer.log"
        $taskLogs.Add($taskLog)
        $taskArgs = @($taskProject,$taskMap,'-game','-nosteam',"-port=$Port",'-MCIceColdReworkSmoke',
            "-MCIceExpectedPlayers=$taskPeers",'-unattended','-nosound','-nosplash','-nop4',"-abslog=$taskLog")
        if ($RequireVFX) { $taskArgs += '-MCIceRequireVFX' }
        if ($Mode -eq 'Visual') {
            $taskArgs += '-MCIceEventCapture'
            if ($taskPeer -eq 0) {
                $taskArgs += @("-MCIceCaptureDir=$taskCapture",'-RenderOffscreen','-windowed','-ForceRes',
                    '-ResX=1600','-ResY=1000','-NoScreenMessages',
                    '-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,r.ScreenPercentage 100')
            } else { $taskArgs += @('-nullrhi','-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0') }
        } else { $taskArgs += @('-nullrhi','-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0') }
        $taskProcess = Start-ColdProcess $taskArgs
        Write-Output "START role=$(if($taskPeer -eq 0){'authority'}else{'client'}) pid=$($taskProcess.Id) log=$taskLog"
        if ($taskPeer -eq 0 -and $taskPeers -gt 1) {
            $taskListenLimit = if ($Mode -eq 'Visual') { 500 } else { 90 }
            $taskListenDeadline = (Get-Date).AddSeconds([Math]::Min($taskListenLimit,$TimeoutSeconds))
            while (-not ((Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern "listening on port $Port\b" -Quiet))) {
                Assert-NoColdFailure
                $taskProcess.Refresh()
                if ($taskProcess.HasExited -or (Get-Date) -ge $taskListenDeadline) { throw "Host did not listen on $Port`: $taskLog" }
                Start-Sleep -Milliseconds 500
            }
        }
    }
    $taskInvocations | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Invocation.json') -Encoding utf8
    do {
        Assert-NoColdFailure
        $taskRunning = $false
        foreach ($taskProcess in $taskOwnedProcesses) {
            $taskProcess.Refresh()
            if ($taskProcess.HasExited) {
                $taskProcess.WaitForExit()
                if ($taskProcess.ExitCode -ne 0) { throw "Owned Unreal process $($taskProcess.Id) exited $($taskProcess.ExitCode): $taskRunFolder" }
            } else { $taskRunning = $true }
        }
        if ($taskRunning) {
            if ((Get-Date) -ge $taskDeadline) { throw "Owned cold processes timed out after $TimeoutSeconds seconds: $taskRunFolder" }
            Start-Sleep -Milliseconds 500
        }
    } while ($taskRunning)
    $taskResults = @(for ($taskPeer = 0; $taskPeer -lt $taskPeers; ++$taskPeer) {
        Assert-ColdLog $taskLogs[$taskPeer] $(if ($taskPeer -eq 0) { 'authority' } else { 'client' })
    })
    if ($Mode -eq 'Visual') {
        $taskTimeline = Join-Path $taskCapture 'FrameTimes.csv'
        if (-not (Test-Path -LiteralPath $taskTimeline)) { throw "Missing rendered timeline: $taskTimeline" }
        $taskFrames = @(Import-Csv -LiteralPath $taskTimeline)
        if ($taskFrames.Count -lt 100) { throw "Expected at least 100 real rendered frames: $taskCapture" }
        $taskNames = [Collections.Generic.HashSet[string]]::new()
        $taskPreviousTime = [double]::NegativeInfinity
        foreach ($taskFrame in $taskFrames) {
            if ($taskFrame.frame -notmatch '^Frame\d+\.png$' -or -not $taskNames.Add($taskFrame.frame)) { throw "Invalid captured filename: $($taskFrame.frame)" }
            $taskTime = 0.0
            if (-not [double]::TryParse($taskFrame.server_time,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$taskTime) -or
                [double]::IsNaN($taskTime) -or [double]::IsInfinity($taskTime) -or $taskTime -le $taskPreviousTime) { throw "Invalid capture time: $taskTimeline" }
            $taskPreviousTime = $taskTime
            $taskImage = Join-Path $taskCapture $taskFrame.frame
            if (-not (Test-Path -LiteralPath $taskImage) -or (Get-Item -LiteralPath $taskImage).Length -lt 1024) { throw "Missing actual rendered frame: $taskImage" }
        }
        foreach ($taskStage in @('COLD_BLOCKED_ZONE_AXE','COLD_SELF_AXE_RESCUE','COLD_CORE_COMBAT','COLD_CORE_COMPLETE')) {
            if (-not ($taskFrames | Where-Object { $_.stage -eq $taskStage })) { throw "Capture missed gameplay stage '$taskStage': $taskTimeline" }
        }
        $taskFrameCount = $taskFrames.Count
    }
} catch { $taskFailure = $_.Exception.Message }
finally {
    $taskExitCodes = @()
    foreach ($taskProcess in $taskOwnedProcesses) {
        $taskProcess.Refresh()
        if (-not $taskProcess.HasExited) { $taskProcess.Kill();$taskProcess.WaitForExit(5000) | Out-Null }
        $taskExitCodes += [pscustomobject]@{pid=$taskProcess.Id;exitCode=if($taskProcess.HasExited){$taskProcess.ExitCode}else{$null}}
        $taskProcess.Dispose()
    }
    $taskStatus = if ($taskFailure) { 'FAIL' } else { 'PASS' }
    [pscustomobject]@{
        status=$taskStatus;mode=$Mode;expectedPlayers=$taskPeers;timeoutSeconds=$TimeoutSeconds;port=$Port;requireVFX=[bool]$RequireVFX
        startedAt=$taskStartedAt.ToString('o');finishedAt=(Get-Date).ToString('o')
        processes=$taskExitCodes;logs=@($taskLogs);checks=$taskResults;frames=$taskFrameCount
        captureDirectory=if($Mode -eq 'Visual'){$taskCapture}else{$null};error=$taskFailure
        scope='Focused saved L_Mouth cold rework with production timers and native Enhanced Input axe contacts. Network checks include remote own foot freeze and rescue. Visual uses real-time selected PNG stages. No cook/package or unrelated F3 event coverage.'
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.json') -Encoding utf8
    @("$taskStatus mode=$Mode peers=$taskPeers frames=$taskFrameCount",$taskResults,$taskFailure) |
        Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.txt') -Encoding utf8
}
if ($taskFailure) { throw "$taskFailure See $taskRunFolder\Validation.json" }
Write-Output "PASS: $Mode focused cold integration. Reports: $taskRunFolder"
if ($Mode -eq 'Visual') { Write-Output "CAPTURE_DIRECTORY=$taskCapture" }
