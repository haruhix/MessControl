param(
    [string]$EngineRoot = 'E:\UE\UE_5.8',
    [ValidateSet('Runtime','Network','Visual')][string]$Mode = 'Runtime',
    [ValidateRange(1,2)][int]$Players = 1,
    [string]$OutputDirectory = '',
    [ValidateRange(1024,65535)][int]$Port = 7796,
    [ValidateRange(0,1800)][int]$TimeoutSeconds = 0
)
$ErrorActionPreference = 'Stop'
if ($TimeoutSeconds -eq 0) { $TimeoutSeconds = if ($Mode -eq 'Visual') { 900 } else { 300 } }
$taskProjectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskProject = Join-Path $taskProjectRoot 'MessControl.uproject'
$taskEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $taskEditor)) { throw "Editor executable missing: $taskEditor" }
if (-not (Test-Path -LiteralPath $taskProject)) { throw "Project missing: $taskProject" }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskProjectRoot 'Saved\FogBrawlValidation\runtime' }
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

function ConvertTo-FogArgument([string]$Value) {
    # Start-Process joins ArgumentList; quote individual arguments using the Windows CRT rules.
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    return '"' + ($Value -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

function Start-FogProcess([string[]]$Arguments) {
    $taskArgumentString = ($Arguments | ForEach-Object { ConvertTo-FogArgument $_ }) -join ' '
    $taskStarted = Start-Process -FilePath $taskEditor -ArgumentList $taskArgumentString -WindowStyle Hidden -PassThru
    $taskOwnedProcesses.Add($taskStarted)
    $taskInvocations.Add([pscustomobject]@{pid=$taskStarted.Id;arguments=$Arguments})
    return $taskStarted
}

function Assert-FogLog([string]$Log,[string]$Role) {
    if (-not (Test-Path -LiteralPath $Log)) { throw "Missing runtime log: $Log" }
    $taskLines = Get-Content -LiteralPath $Log
    if ($taskLines -match 'MC_FOG_SMOKE_FAIL|Assertion failed:|Fatal error:|Ensure condition failed:') { throw "Runtime failure in $Log" }
    $taskPass = @($taskLines | Where-Object { $_ -match "MC_FOG_SMOKE_PASS role=$Role\b" })
    if ($taskPass.Count -ne 1) { throw "Expected exactly one fog smoke pass for $Role`: $Log" }
    if ($Role -eq 'client') {
        if ($taskPass[0] -notmatch 'remote_seen=2047\b') { throw "Remote peer missed required replicated protocol: $Log" }
    } else {
        $taskChecks = @($taskLines | Where-Object { $_ -match 'MC_FOG_CHECK PASS strike=\d+\b' })
        if ($taskChecks.Count -ne 5) { throw "Expected five checked actual strikes, got $($taskChecks.Count): $Log" }
        foreach ($taskStrike in 1..5) {
            if (@($taskChecks | Where-Object { $_ -match "strike=$taskStrike\b" }).Count -ne 1) {
                throw "Actual strike $taskStrike was missing or checked twice: $Log"
            }
        }
        foreach ($taskToken in @('five_strikes=5','native_push=1','native_recovery=1','proximity=1','complete=1','reset=1')) {
            if ($taskPass[0] -notmatch ([regex]::Escape($taskToken)+'\b')) { throw "Authority missed '$taskToken': $Log" }
        }
        if (@($taskLines | Where-Object { $_ -match 'MC_FOG_F3_START PASS\b' }).Count -ne 1) { throw "Missing real F3 launch validation: $Log" }
        if (@($taskLines | Where-Object { $_ -match 'MC_FOG_CHECK PASS consumed_retarget=1\b' }).Count -ne 1) { throw "Missing real consumed-target validation: $Log" }
        if (@($taskLines | Where-Object { $_ -match 'MC_FOG_CHECK PASS f3_stop_full_cleanup=1\b' }).Count -ne 1) { throw "Missing real F3 cleanup validation: $Log" }
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
        $taskArgs = @($taskProject,$taskMap,'-game','-nosteam',"-port=$Port",'-MCFogBrawlSmoke',
            "-MCFogExpectedPlayers=$taskPeers",'-unattended','-nosound','-nosplash','-nop4',"-abslog=$taskLog")
        if ($Mode -eq 'Visual') {
            # Both visual peers use the longer, slow-motion smoke deadline. Only the host renders frames.
            $taskArgs += '-MCFogCapture'
            if ($taskPeer -eq 0) {
                $taskArgs += @("-MCFogCaptureDir=$taskCapture",'-RenderOffscreen','-windowed','-ForceRes',
                    '-ResX=1600','-ResY=1000','-NoScreenMessages',
                    '-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,r.ScreenPercentage 100')
            } else {
                $taskArgs += @('-nullrhi','-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0')
            }
        } else {
            $taskArgs += @('-nullrhi','-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0')
        }
        $taskProcess = Start-FogProcess $taskArgs
        Write-Output "START role=$(if($taskPeer -eq 0){'authority'}else{'client'}) pid=$($taskProcess.Id) log=$taskLog"
        if ($taskPeer -eq 0 -and $taskPeers -gt 1) {
            $taskListenDeadline = (Get-Date).AddSeconds([Math]::Min(90,$TimeoutSeconds))
            while (-not ((Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern "listening on port $Port\b" -Quiet))) {
                $taskProcess.Refresh()
                if ($taskProcess.HasExited -or (Get-Date) -ge $taskListenDeadline) { throw "Host did not listen on $Port`: $taskLog" }
                Start-Sleep -Milliseconds 500
            }
        }
    }
    $taskInvocations | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Invocation.json') -Encoding utf8
    do {
        $taskRunning = $false
        foreach ($taskProcess in $taskOwnedProcesses) {
            $taskProcess.Refresh()
            if ($taskProcess.HasExited) {
                $taskProcess.WaitForExit()
                if ($taskProcess.ExitCode -ne 0) { throw "Owned Unreal process $($taskProcess.Id) exited $($taskProcess.ExitCode): $taskRunFolder" }
            } else { $taskRunning = $true }
        }
        if ($taskRunning) {
            if ((Get-Date) -ge $taskDeadline) { throw "Owned fog processes timed out after $TimeoutSeconds seconds: $taskRunFolder" }
            Start-Sleep -Milliseconds 500
        }
    } while ($taskRunning)
    $taskResults = @(for ($taskPeer = 0; $taskPeer -lt $taskPeers; ++$taskPeer) {
        Assert-FogLog $taskLogs[$taskPeer] $(if ($taskPeer -eq 0) { 'authority' } else { 'client' })
    })
    if ($Mode -eq 'Visual') {
        $taskTimeline = Join-Path $taskCapture 'FrameTimes.csv'
        if (-not (Test-Path -LiteralPath $taskTimeline)) { throw "Missing rendered timeline: $taskTimeline" }
        $taskFrames = @(Import-Csv -LiteralPath $taskTimeline)
        $taskImages = @(Get-ChildItem -LiteralPath $taskCapture -File -Filter 'Frame*.png')
        if ($taskFrames.Count -lt 100 -or $taskImages.Count -lt 100) { throw "Expected at least 100 actual PNG frames and timeline rows: $taskCapture" }
        foreach ($taskField in @('frame','server_time','stage')) {
            if ($taskField -notin $taskFrames[0].PSObject.Properties.Name) { throw "Timeline is missing '$taskField': $taskTimeline" }
        }
        $taskNames = [Collections.Generic.HashSet[string]]::new()
        $taskPreviousTime = [double]::NegativeInfinity
        foreach ($taskFrame in $taskFrames) {
            if ($taskFrame.frame -notmatch '^Frame\d+\.png$' -or -not $taskNames.Add($taskFrame.frame)) { throw "Invalid or duplicate captured filename: $($taskFrame.frame)" }
            $taskTime = 0.0
            if (-not [double]::TryParse($taskFrame.server_time,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$taskTime) -or
                [double]::IsNaN($taskTime) -or [double]::IsInfinity($taskTime) -or $taskTime -le $taskPreviousTime) {
                throw "Capture times are invalid or not strictly increasing: $taskTimeline"
            }
            $taskPreviousTime = $taskTime
            if ([string]::IsNullOrWhiteSpace($taskFrame.stage)) { throw "Capture row has no gameplay stage: $taskTimeline" }
            $taskImage = Join-Path $taskCapture $taskFrame.frame
            if (-not (Test-Path -LiteralPath $taskImage) -or (Get-Item -LiteralPath $taskImage).Length -lt 1024) { throw "Missing/empty actual rendered frame: $taskImage" }
        }
        $taskFrameCount = $taskFrames.Count
    }
} catch {
    $taskFailure = $_.Exception.Message
} finally {
    $taskExitCodes = @()
    foreach ($taskProcess in $taskOwnedProcesses) {
        $taskProcess.Refresh()
        if (-not $taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit(5000) | Out-Null }
        $taskExitCodes += [pscustomobject]@{pid=$taskProcess.Id;exitCode=if($taskProcess.HasExited){$taskProcess.ExitCode}else{$null}}
        $taskProcess.Dispose()
    }
    $taskStatus = if ($taskFailure) { 'FAIL' } else { 'PASS' }
    [pscustomobject]@{
        status=$taskStatus;mode=$Mode;expectedPlayers=$taskPeers;timeoutSeconds=$TimeoutSeconds;port=$Port
        startedAt=$taskStartedAt.ToString('o');finishedAt=(Get-Date).ToString('o')
        processes=$taskExitCodes;logs=@($taskLogs);checks=$taskResults;frames=$taskFrameCount
        captureDirectory=if($Mode -eq 'Visual'){$taskCapture}else{$null};error=$taskFailure
        scope='Saved L_Mouth F3 runtime; two players require independent peer input and replicated checks. Visual renders host PNG frames and timestamps; its remote peer runs with nullrhi. No cook/package coverage.'
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.json') -Encoding utf8
    @("$taskStatus mode=$Mode peers=$taskPeers frames=$taskFrameCount",$taskResults,$taskFailure) |
        Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.txt') -Encoding utf8
}
if ($taskFailure) { throw "$taskFailure See $taskRunFolder\Validation.json" }
Write-Output "PASS: $Mode fog integration. Reports: $taskRunFolder"
if ($Mode -eq 'Visual') { Write-Output "CAPTURE_DIRECTORY=$taskCapture" }
