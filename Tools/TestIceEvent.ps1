param(
    [string]$EngineRoot = 'E:\UE\UE_5.8',
    [ValidateSet('Runtime','Network','Visual')][string]$Mode = 'Runtime',
    [string]$OutputDirectory = '',
    [int]$Port = 7791,
    [int]$TimeoutSeconds = 0,
    [switch]$StuckFoodDiagnostic
)
$ErrorActionPreference = 'Stop'
if ($TimeoutSeconds -le 0) { $TimeoutSeconds = if ($Mode -eq 'Visual') { 900 } else { 300 } }
$taskProjectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskProject = Join-Path $taskProjectRoot 'MessControl.uproject'
$taskEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $taskEditor)) { throw "Editor executable missing: $taskEditor" }
if (-not (Test-Path -LiteralPath $taskProject)) { throw "Project missing: $taskProject" }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskProjectRoot 'Saved\IceEventValidation' }
$taskRunFolder = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ("{0}_{1}" -f $Mode,(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
New-Item -ItemType Directory -Path $taskRunFolder -Force | Out-Null
$taskOwnedProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()
$taskLogs = [Collections.Generic.List[string]]::new()
function Start-IceProcess([string[]]$Arguments) {
    $taskStarted = Start-Process -FilePath $taskEditor -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    $taskOwnedProcesses.Add($taskStarted)
    return $taskStarted
}
function Wait-IceProcess([Diagnostics.Process]$Process) {
    $taskDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $Process.HasExited) {
        if ((Get-Date) -ge $taskDeadline) { throw "Owned Unreal process $($Process.Id) timed out: $taskRunFolder" }
        Start-Sleep -Milliseconds 500
        $Process.Refresh()
    }
    $Process.WaitForExit()
    if ($Process.ExitCode -ne 0) { throw "Owned Unreal process $($Process.Id) exited $($Process.ExitCode): $taskRunFolder" }
}
function Assert-IceLog([string]$Log,[string]$Role) {
    if (-not (Test-Path -LiteralPath $Log)) { throw "Missing runtime log: $Log" }
    $taskLines = Get-Content -LiteralPath $Log
    if ($taskLines -match 'MC_ICE_SMOKE_FAIL|Assertion failed:|Fatal error:|Ensure condition failed:') { throw "Runtime failure in $Log" }
    $taskPass = @($taskLines | Where-Object { $_ -match "MC_ICE_SMOKE_PASS role=$Role\b" })
    if ($taskPass.Count -ne 1) { throw "Expected exactly one smoke pass for $Role`: $Log" }
    if ($Role -eq 'client' -and $taskPass[0] -notmatch 'remote_seen=127\b') { throw "Remote peer missed protocol: $Log" }
    if ($Role -eq 'authority') {
        $taskEvents = @($taskLines | Where-Object { $_ -match 'MC_ICE_F3_CHECK PASS action=' -and $_ -notmatch 'action=StartStep' })
        if ($taskEvents.Count -ne 20) { throw "Expected 20 F3 event starts, got $($taskEvents.Count): $Log" }
    }
    return $taskPass[0]
}
try {
    $taskPeers = if ($Mode -eq 'Network') { 2 } else { 1 }
    if ($taskPeers -gt 1 -and (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)) { throw "UDP port $Port is already occupied; choose another -Port" }
    $taskCapture = Join-Path $taskRunFolder 'Frames'
    for ($taskPeer = 0; $taskPeer -lt $taskPeers; ++$taskPeer) {
        $taskMap = if ($taskPeer -eq 0) { if ($taskPeers -gt 1) { '/Game/Maps/L_Mouth?listen?Seed=41' } else { '/Game/Maps/L_Mouth?Seed=41' } } else { "127.0.0.1:$Port" }
        $taskLog = Join-Path $taskRunFolder "Peer$taskPeer.log"
        $taskLogs.Add($taskLog)
        $taskArgs = @("`"$taskProject`"",$taskMap,'-game','-nosteam',"-port=$Port",'-MCIceEventSmoke',
            "-MCIceExpectedPlayers=$taskPeers",'-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"")
        if ($StuckFoodDiagnostic) { $taskArgs += '-MCStuckFoodDiagnostic' }
        if ($Mode -eq 'Visual') {
            $taskArgs += @('-MCIceEventCapture',"`"-MCIceCaptureDir=$taskCapture`"",'-RenderOffscreen','-windowed','-ForceRes',
                '-ResX=1600','-ResY=1000','-NoScreenMessages',
                '"-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,r.ScreenPercentage 100"')
        } else {
            $taskArgs += @('-nullrhi','"-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0"')
        }
        $taskProcess = Start-IceProcess $taskArgs
        if ($taskPeer -eq 0 -and $taskPeers -gt 1) {
            $taskListenDeadline = (Get-Date).AddSeconds(90)
            while (-not ((Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern "listening on port $Port\b" -Quiet))) {
                $taskProcess.Refresh()
                if ($taskProcess.HasExited -or (Get-Date) -ge $taskListenDeadline) { throw "Host did not listen on $Port`: $taskLog" }
                Start-Sleep -Milliseconds 500
            }
        }
    }
    foreach ($taskProcess in $taskOwnedProcesses) { Wait-IceProcess $taskProcess }
    $taskResults = for ($taskPeer = 0; $taskPeer -lt $taskPeers; ++$taskPeer) {
        Assert-IceLog $taskLogs[$taskPeer] $(if ($taskPeer -eq 0) { 'authority' } else { 'client' })
    }
    if ($Mode -eq 'Visual') {
        $taskTimeline = Join-Path $taskCapture 'FrameTimes.csv'
        if (-not (Test-Path -LiteralPath $taskTimeline)) { throw "Missing rendered timeline: $taskTimeline" }
        $taskFrames = @(Import-Csv -LiteralPath $taskTimeline)
        if ($taskFrames.Count -lt 100) { throw "Too few real runtime frames: $taskTimeline" }
        foreach ($taskFrame in $taskFrames) {
            $taskImage = Join-Path $taskCapture $taskFrame.frame
            if (-not (Test-Path -LiteralPath $taskImage) -or (Get-Item -LiteralPath $taskImage).Length -lt 1024) { throw "Missing/empty rendered frame: $taskImage" }
        }
    }
    $taskResults | Set-Content -LiteralPath (Join-Path $taskRunFolder 'Validation.txt') -Encoding utf8
    Write-Output "PASS: $Mode F3 and winter integration. Reports: $taskRunFolder"
    if ($Mode -eq 'Visual') { Write-Output "CAPTURE_DIRECTORY=$taskCapture" }
} finally {
    foreach ($taskProcess in $taskOwnedProcesses) {
        $taskProcess.Refresh()
        if (-not $taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id -Force }
    }
}
