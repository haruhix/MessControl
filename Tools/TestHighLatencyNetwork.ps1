<#
.SYNOPSIS
Runs one existing network fixture at explicitly defined added RTTs.
.EXAMPLE
./Tools/TestHighLatencyNetwork.ps1 -Scenario Locomotion -Profile Baseline,RTT200,RTT400 -DryRun
.EXAMPLE
./Tools/TestHighLatencyNetwork.ps1 -Scenario Spray -Profile RTT400JitterLoss -Trace
.NOTES
No build, cook, editor automation, Steam connection or visual acceptance is performed.
PktLag delays outgoing packets. Half the requested added RTT is applied to each
peer, so do not also add incoming lag. Actual measured ping is fixture-dependent.
ActiveRagdoll supports two to four peers; ActiveSkills and Foundation require exactly two.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Locomotion','Grip','Food','Spray','WeaponPvP','ActiveRagdoll','ActiveSkills','Foundation')]
    [string]$Scenario,
    [ValidateSet('Baseline','RTT200','RTT400','RTT400JitterLoss','Custom')]
    [string[]]$Profile=@('Baseline','RTT200','RTT400'),
    [ValidateRange(0,1000)][int]$AddedRoundTripMs=400,
    [ValidateRange(0,200)][int]$OneWayJitterMs=0,
    [ValidateRange(0,10)][int]$PacketLossPercent=0,
    [string]$EngineRoot='E:/UE/UE_5.8',
    [string]$OutputDirectory,
    [string]$CacheDirectory,
    [ValidateRange(1024,65535)][int]$Port=17977,
    [ValidateRange(30,600)][int]$TimeoutSeconds=300,
    [ValidateRange(30,360)][int]$HostFPS=60,
    [ValidateRange(30,360)][int]$ClientFPS=60,
    [ValidateRange(2,4)][int]$PeerCount=4,
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$VideoName='',
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$WindowVideoName='',
    [ValidateSet(0,1)][int]$WindowVideoPeer=1,
    [switch]$RagdollSoftOnly,
    [switch]$RagdollStaticFloor,
    [switch]$CaptureCorrections,
    [switch]$Trace,
    [switch]$DryRun
)
$ErrorActionPreference='Stop'
$taskProjectRoot=Split-Path -Parent $PSScriptRoot
if($VideoName -and $WindowVideoName){throw 'Choose either native VideoName or WindowVideoName capture.'}
if($PSBoundParameters.ContainsKey('WindowVideoPeer') -and -not $WindowVideoName){throw 'WindowVideoPeer requires WindowVideoName mode.'}
if($WindowVideoName -and $WindowVideoPeer -ge $PeerCount){throw 'WindowVideoPeer must identify a participating peer.'}
$taskWindowRecorder='E:/CodexWork/MessControl/Validation/HighLatencyProbeTools/record_owned_network_window.py'
$taskPython=$null
if($WindowVideoName){
    if(-not (Test-Path -LiteralPath $taskWindowRecorder)){throw "Owned-window recorder missing: $taskWindowRecorder"}
    $taskPythonCommand=Get-Command python -CommandType Application -ErrorAction Stop | Select-Object -First 1
    $taskPython=$taskPythonCommand.Source
    if($taskPython -isnot [string] -or [string]::IsNullOrWhiteSpace($taskPython) -or -not (Test-Path -LiteralPath $taskPython -PathType Leaf)){
        throw 'Owned-window recorder requires one resolved Python executable FilePath string.'
    }
}
if(-not $CacheDirectory){$CacheDirectory=Join-Path $taskProjectRoot 'Saved/DerivedDataCache'}
$taskCache=[IO.Path]::GetFullPath($CacheDirectory)
if([IO.Path]::GetPathRoot($taskCache) -ine 'E:\'){throw 'CacheDirectory must be on E:.'}
$taskRunId=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
if(-not $OutputDirectory){$OutputDirectory="E:/CodexWork/MessControl/Validation/HighLatency/$taskRunId"}
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if([IO.Path]::GetPathRoot($taskOutput) -ine 'E:\'){throw 'OutputDirectory must be on E:.'}
if(Test-Path -LiteralPath $taskOutput){throw 'Choose a new OutputDirectory; existing evidence is never overwritten.'}
$taskExe=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$taskProject=Join-Path $taskProjectRoot 'MessControl.uproject'
if(-not (Test-Path -LiteralPath $taskExe)){throw "Engine executable missing: $taskExe"}
if(-not (Test-Path -LiteralPath $taskProject)){throw "Project missing: $taskProject"}
$taskDefinitions=@{
    Locomotion=@{Flag='-MCLocomotionTest';Marker='MC_VALIDATION_(PASS|FAIL) LOCOMOTION';Scope='Local-owner movement on normal/sticky/slippery surfaces; speeds and finite poses. No correction-distance or visual smoothness gate.'}
    Grip=@{Flag='-MCGripTest';Marker='MC_VALIDATION_(PASS|FAIL) GRIP';Scope='Replicated grip/contact and hand-pose checks. Fixture acquires food on the server; remote input-to-grab latency and moving cooperative carry are not covered.'}
    Food=@{Flag='-MCFoodNetwork';Marker='MC_VALIDATION_(PASS|FAIL) FOOD_NETWORK';Scope='Real E/Handle collection, replicated three-piece stacks, hop/stretch/yawn/collision and camera-acquired spray. Moving cooperative carry and visible food jitter require a separate scenario.'}
    Spray=@{Flag='-MCSprayNetworkTest';Marker='MC_VALIDATION_(PASS|FAIL) SPRAY_NETWORK';Scope='Real remote-owner spray press/release and replicated state. NullRHI does not verify visible spray or aim smoothness.'}
    WeaponPvP=@{Flag='-MCWeaponPvP';Marker='MC_VALIDATION_(PASS|FAIL) WEAPON_PVP';Scope='Remote-owner brush/pickaxe/knife contact and replicated damage; stationary targets, no moving-target lag compensation or projectile/rocket/hook coverage.'}
    ActiveRagdoll=@{Flag='-MCActiveRagdollTest';Marker='MC_ACTIVE_RAGDOLL_(PASS|FAIL)';Scope='Existing active-ragdoll/physical pose fixture and its logged ping samples; no gameplay-camera motion acceptance.'}
    ActiveSkills=@{Flag='-MCActiveSkillNetworkTest';Marker='MC_VALIDATION_(PASS|FAIL) ACTIVE_SKILL_NETWORK';Scope='Two real peers exercise aimed RMB/LMB Hook in both directions and remote Rocket, accepted events, cleanup and recovery; visual acceptance requires inspecting actual recording.'}
    Foundation=@{Flag='-MCNetworkFoundationTest';Marker='MC_VALIDATION_(PASS|FAIL) NETWORK_FOUNDATION';Scope='Two real peers exercise common native locomotion, small ApplyHit, constant and steered external leases, body ownership takeover, explicit reset and recovery. Render-step samples include intentional resets; actual client body/camera motion requires separate review.'}
}
$taskDefinition=$taskDefinitions[$Scenario]
if($Scenario -in @('ActiveSkills','Foundation')){
    if($PeerCount -ne 2){throw "$Scenario requires exactly two peers; pass -PeerCount 2."}
}elseif($PeerCount -ne 4 -and $Scenario -ne 'ActiveRagdoll'){throw "$Scenario requires four peers in its existing native fixture; only ActiveRagdoll supports PeerCount 2 or 3."}
if($RagdollSoftOnly -and $Scenario -ne 'ActiveRagdoll'){throw 'RagdollSoftOnly is supported only by the ActiveRagdoll scenario.'}
if($RagdollStaticFloor -and $Scenario -ne 'ActiveRagdoll'){throw 'RagdollStaticFloor is supported only by the ActiveRagdoll scenario.'}
$taskProfiles=@()
foreach($taskName in ($Profile | Select-Object -Unique)){
    $taskRTT=0;$taskVariance=0;$taskLoss=0
    switch($taskName){
        'RTT200' {$taskRTT=200}
        'RTT400' {$taskRTT=400}
        'RTT400JitterLoss' {$taskRTT=400;$taskVariance=30;$taskLoss=1}
        'Custom' {$taskRTT=$AddedRoundTripMs;$taskVariance=$OneWayJitterMs;$taskLoss=$PacketLossPercent}
    }
    if($taskRTT % 2 -ne 0){throw 'AddedRoundTripMs must be even so both outgoing directions use the same integer delay.'}
    if($taskVariance -gt $taskRTT/2 -or ($taskVariance -gt 0 -and $taskRTT -eq 0)){throw 'OneWayJitterMs must not exceed the per-direction lag.'}
    $taskProfiles+=@{name=$taskName;added_rtt_ms=$taskRTT;outgoing_lag_ms_per_peer=$taskRTT/2;outgoing_variance_ms_per_peer=$taskVariance;outgoing_loss_percent_per_peer=$taskLoss}
}
$taskTemp="E:/CodexTemp/MessControlHighLatency/$taskRunId"
# UE 5.8 CharacterMovementComponent.cpp:11423 and :12471 emit these warnings.
# Absence of matching warnings is not evidence of visually smooth motion.
function Get-HighLatencyCorrectionStats {
    param([AllowEmptyCollection()][string[]]$Lines)
    $taskDistances=@();$taskOverflows=0
    foreach($taskLine in $Lines){
        if($taskLine -match '\*\*\* Client: Error for .+? at Time=[+-]?[\d.]+\s+is\s+(?<cm>[\d.]+)\s+LocDiff\(.*\)\s+SavedMoves\s+(?<saved>\d+)\b'){
            $taskDistances+=[double]::Parse($Matches.cm,[Globalization.CultureInfo]::InvariantCulture)
        }
        if($taskLine -match 'CreateSavedMove: Hit limit of \d+ saved moves \(timing out or very bad ping\?\)'){$taskOverflows++}
    }
    $taskSorted=@($taskDistances | Sort-Object)
    return [ordered]@{
        status=$(if($taskSorted.Count){'samples_observed'}else{'no_matching_correction_samples'})
        count=$taskSorted.Count
        max_cm=$(if($taskSorted.Count){$taskSorted[-1]}else{$null})
        p95_cm=$(if($taskSorted.Count){$taskSorted[[int][Math]::Ceiling(.95*$taskSorted.Count)-1]}else{$null})
        p95_method='nearest_rank';saved_move_overflow_count=$taskOverflows
        scope='Logged CMC owning-client corrections only; zero samples does not prove absence of visible jitter.'
    }
}
function Get-HighLatencyNetworkTelemetry {
    param([AllowEmptyCollection()][string[]]$Lines)
    $taskPings=@();$taskDeltas=@();$taskSamples=0;$taskReady=0;$taskAcknowledged=0;$taskBlocked=0
    foreach($taskLine in $Lines){
        if($taskLine -notmatch 'MC_NET_TELEMETRY net=\d+ world_t=[\d.]+ dt_ms=(?<dt>[\d.]+) ping_ms=(?<ping>[\d.]+) ready=(?<ready>[01]) ack=(?<ack>[01]) move_blocked=(?<blocked>[01])\b'){continue}
        $taskSamples++;$taskReady+=[int]$Matches.ready;$taskAcknowledged+=[int]$Matches.ack;$taskBlocked+=[int]$Matches.blocked
        $taskDeltas+=[double]::Parse($Matches.dt,[Globalization.CultureInfo]::InvariantCulture)
        $taskPing=[double]::Parse($Matches.ping,[Globalization.CultureInfo]::InvariantCulture)
        if($taskPing -gt 0){$taskPings+=$taskPing}
    }
    if(-not $taskSamples){return $null}
    $taskDeltaSorted=@($taskDeltas | Sort-Object)
    $taskMeasuredPing=$null
    if($taskPings.Count){
        $taskPingStats=$taskPings | Measure-Object -Minimum -Maximum -Average
        $taskMeasuredPing=@{source='MC_NET_TELEMETRY';min_ms=$taskPingStats.Minimum;avg_ms=$taskPingStats.Average;max_ms=$taskPingStats.Maximum;samples=$taskPings.Count}
    }
    return [ordered]@{
        source='MC_NET_TELEMETRY';samples=$taskSamples;ready_samples=$taskReady;acknowledged_samples=$taskAcknowledged;move_blocked_samples=$taskBlocked
        sampled_dt_max_ms=$taskDeltaSorted[-1];sampled_dt_p95_ms=$taskDeltaSorted[[int][Math]::Ceiling(.95*$taskSamples)-1]
        delta_scope='Last-frame delta sampled every 0.5 real seconds; not an all-frame hitch measurement.'
        measured_ping=$taskMeasuredPing
    }
}
$taskModule=Get-Item -LiteralPath (Join-Path $taskProjectRoot 'Binaries/Win64/UnrealEditor-MessControl.dll') -ErrorAction SilentlyContinue
$taskReport=[ordered]@{
    schema=1;run_id=$taskRunId;started_utc=[DateTime]::UtcNow.ToString('o');scenario=$Scenario
    status=$(if($DryRun){'dry_run'}else{'running'});dry_run=[bool]$DryRun;peer_count=$PeerCount;port=$Port;host_fps=$HostFPS;client_fps=$ClientFPS;capture_corrections=[bool]$CaptureCorrections
    host_role='local listen server; geography and Steam routing are not simulated; selected symmetric packet emulation applies to remote links'
    fixture_scope=$taskDefinition.Scope;renderer=$(if($WindowVideoName){"Peer$WindowVideoPeer windowed HWND capture; other peers NullRHI"}elseif($VideoName){'Peer1 RenderOffscreen; other peers NullRHI'}else{'NullRHI'});video_name=$VideoName;window_video_name=$WindowVideoName;window_video_peer=$(if($WindowVideoName){$WindowVideoPeer}else{$null});window_capture_role=$(if($WindowVideoName){if($WindowVideoPeer -eq 0){'listen_host'}else{'client'}}else{$null});profiles=$taskProfiles;results=@()
    host_dev_ui_for_fixture=($Scenario -eq 'ActiveRagdoll')
    skip_ubt_sdk_setup=$true
    cache_directory=$taskCache
    cache_scope='Reuses the selected cache without clearing it. Warm-cache network evidence is not a cold-start or prewarm-readiness guarantee.'
    network_telemetry_requested=$true
    ragdoll_soft_only=[bool]$RagdollSoftOnly
    ragdoll_static_floor=[bool]$RagdollStaticFloor
    fixture_readiness_scope=$(if($Scenario -in @('Locomotion','Grip','Spray','WeaponPvP')){'Legacy fixture has no complete presentation-ready/ACK/arrival gate. Its result does not establish stable carry/hook motion under fully prepared gameplay.'}else{'Readiness and scenario coverage are defined by the selected native fixture.'})
    startup_connection_timeout_seconds=180
    connection_timeout_seconds=180
    video_after_ready=[bool]$VideoName
    window_video_after_ready=[bool]$WindowVideoName
    source_validation='Current binaries only; this runner does not build or certify that DLLs match source.'
    runtime_module_utc=$(if($taskModule){$taskModule.LastWriteTimeUtc.ToString('o')}else{$null})
    limitations=@('Emulates packet delay/variance/loss on loopback, not actual Steam routing or geography.',
        'Requested added RTT is a configuration, not a measured ping. Only native logged ping is reported as measured.',
        'Correction telemetry is null when not captured; zero matching CMC corrections does not establish visual smoothness.',
        'Fixture pass and recorded frames are functional/capture evidence; visual smoothness remains unaccepted until actual motion is inspected.',
        'Test-only connection timeouts are 180s to tolerate preparation; this runner does not validate production disconnect detection.',
        'Existing fixtures do not certify every hook target/geometry or moving cooperative-carry scenario.')
}
New-Item -ItemType Directory -Path $taskOutput,$taskTemp,$taskCache -Force | Out-Null
$taskReportPath=Join-Path $taskOutput 'Results.json'
function Save-HighLatencyReport { $taskReport | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $taskReportPath -Encoding UTF8 }
$taskPreviousTemp=$env:TEMP;$taskPreviousTmp=$env:TMP;$taskPreviousSDKSkip=$env:UE_SKIP_UBT_SDK_SETUP
try{
    $env:TEMP=$taskTemp;$env:TMP=$taskTemp
    # The caller validates the Editor target before this test-only launch. Match
    # TestDashSuction.ps1 and avoid repeating SDK discovery in every game peer.
    $env:UE_SKIP_UBT_SDK_SETUP='1'
    foreach($taskProfile in $taskProfiles){
        $taskFolder=Join-Path $taskOutput $taskProfile.name
        New-Item -ItemType Directory -Path $taskFolder -Force | Out-Null
        $taskResult=[ordered]@{profile=$taskProfile.name;status='planned';peers=@();error=$null;window_capture=$null}
        $taskReport.results+=,$taskResult
        $taskProcesses=@()
        $taskRecorder=$null;$taskRecorderStop=$null
        try{
            if(-not $DryRun){
                # Probe without closing or altering an unrelated listener.
                $taskProbe=[Net.Sockets.UdpClient]::new()
                try{$taskProbe.Client.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Any,$Port))}finally{$taskProbe.Dispose()}
            }
            for($taskPeer=0;$taskPeer -lt $PeerCount;$taskPeer++){
                $taskLog=Join-Path $taskFolder "Peer$taskPeer.log"
                $taskMap=if($taskPeer -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{"127.0.0.1:$Port"}
                $taskExecCmds="t.MaxFPS $(if($taskPeer -eq 0){$HostFPS}else{$ClientFPS}),t.IdleWhenNotForeground 0"
                if($CaptureCorrections){$taskExecCmds+=',p.NetShowCorrections 1,p.NetCorrectionLifetime 0'}
                $taskArguments=@($taskProject,$taskMap,'-game','-nosteam','-MCLegacyDays','-MCHubTransientProfile',
                    "-MCExpectedPlayers=$PeerCount",$taskDefinition.Flag,'-MCNetTelemetry','-nullrhi','-unattended','-nosound','-nosplash','-nop4',
                    "-port=$Port","-abslog=$taskLog","-UserDir=$taskFolder/Peer$taskPeer",
                    '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:InitialConnectTimeout=180',
                    '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ConnectionTimeout=180',
                    '-ddc=NoZenLocalFallback',"-LocalDataCachePath=$taskCache",'-SharedDataCachePath=None',
                    "-PktLag=$($taskProfile.outgoing_lag_ms_per_peer)","-PktLagVariance=$($taskProfile.outgoing_variance_ms_per_peer)",
                    "-PktLoss=$($taskProfile.outgoing_loss_percent_per_peer)",'-PktIncomingLagMin=0','-PktIncomingLagMax=0','-PktIncomingLoss=0',
                    '-PktJitter=0','-PktOrder=0','-PktDup=0','-PktLagMin=0','-PktLagMax=0',
                    "-ExecCmds=$taskExecCmds")
                # The ragdoll driver switches modes via RequestDevAction. Only its
                # authoritative host opts in; remote ClientGuard remains meaningful.
                $taskDevUI=$Scenario -eq 'ActiveRagdoll' -and $taskPeer -eq 0
                if($taskDevUI){$taskArguments+='-MCDevUI'}
                if($RagdollSoftOnly){$taskArguments+='-MCActiveRagdollSoftOnly'}
                if($RagdollStaticFloor){$taskArguments+='-MCActiveRagdollStaticFloor'}
                if($Scenario -eq 'ActiveSkills'){$taskArguments+=@('-MCActiveSkillReview',"-MCActiveSkillNetworkTimeout=$TimeoutSeconds")}
                if($Scenario -eq 'Foundation'){$taskArguments+=@("-MCFoundationNetworkTimeout=$TimeoutSeconds")}
                $taskVideoDirectory=$null;$taskRenderer='NullRHI'
                if($VideoName -and $taskPeer -eq 1){
                    $taskCaptureName=$VideoName+'_'+$taskProfile.name
                    $taskVideoDirectory=[IO.Path]::GetFullPath("$taskFolder/Peer$taskPeer/Saved/ApprovalFrames/$taskCaptureName")
                    $taskRenderer='RenderOffscreen'
                    $taskArguments=@($taskArguments | Where-Object {$_ -ne '-nullrhi'})
                    $taskArguments+=@('-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages',
                        "-MCVideo=$taskCaptureName",'-MCVideoFPS=30','-MCVideoStartSeconds=0','-MCVideoAfterReady')
                }
                $taskWindowVideoDirectory=$null
                if($WindowVideoName -and $taskPeer -eq $WindowVideoPeer){
                    $taskRenderer='Windowed_HWND_WGC'
                    $taskArguments=@($taskArguments | Where-Object {$_ -ne '-nullrhi'})
                    $taskArguments+=@('-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages')
                    $taskWindowVideoDirectory=Join-Path $taskFolder ('WindowVideo_'+$WindowVideoName+'_'+$taskProfile.name)
                    $taskRecorderStop=Join-Path $taskFolder 'WindowVideo.stop'
                    $taskCaptureNetMode=if($taskPeer -eq 0){2}else{3}
                    $taskRecorderArgs=@('-B',$taskWindowRecorder,'--pid',"OWNED_PEER${taskPeer}_PID",'--ready-log',$taskLog,'--output',$taskWindowVideoDirectory,
                        '--stop-file',$taskRecorderStop,'--max-seconds','90','--wait-seconds',[string][Math]::Min(300,$TimeoutSeconds),'--net-mode',[string]$taskCaptureNetMode)
                    $taskResult.window_capture=[ordered]@{status='planned';peer=$taskPeer;capture_role=$(if($taskPeer -eq 0){'listen_host'}else{'client'});readiness_net_mode=$taskCaptureNetMode;helper_pid=$null;helper_arguments=$taskRecorderArgs;manifest=(Join-Path $taskWindowVideoDirectory 'CaptureManifest.json');result=$null}
                }
                if($Trace){$taskArguments+=@('-trace=net,cpu,frame','-NetTrace=1',"-tracefile=$taskFolder/Peer$taskPeer.utrace")}
                $taskPeerResult=[ordered]@{peer=$taskPeer;role=$(if($taskPeer -eq 0){'host'}else{'client'});renderer=$taskRenderer;developer_ui_opt_in=$taskDevUI;video_frames_directory=$taskVideoDirectory;video_frames_written=$null;video_timestamps_present=$null;window_video_directory=$taskWindowVideoDirectory;log=$taskLog;arguments=$taskArguments;pid=$null;exit_code=$null;pass=$false;markers=@();measured_ping=$null;client_corrections=$null;network_telemetry=$null}
                $taskResult.peers+=,$taskPeerResult
                Save-HighLatencyReport
                if($DryRun){continue}
                # Start-Process joins ArgumentList; quote each complete native argument.
                $taskQuotedArguments=@($taskArguments | ForEach-Object {'"'+$_+'"'})
                # The selected game preview is the interactive surface being
                # reviewed. Hidden game windows cannot produce a WGC capture.
                $taskWindowStyle=if($taskWindowVideoDirectory){'Normal'}else{'Hidden'}
                $taskProcess=Start-Process -FilePath $taskExe -ArgumentList $taskQuotedArguments -WorkingDirectory $taskProjectRoot -PassThru -WindowStyle $taskWindowStyle
                $taskProcesses+=,$taskProcess;$taskPeerResult.pid=$taskProcess.Id
                if($taskWindowVideoDirectory){
                    $taskRecorderArgs[3]=[string]$taskProcess.Id
                    $taskRecorder=Start-Process -FilePath $taskPython -ArgumentList @($taskRecorderArgs | ForEach-Object {'"'+$_+'"'}) -WorkingDirectory $taskFolder -PassThru -WindowStyle Hidden `
                        -RedirectStandardOutput (Join-Path $taskFolder 'WindowRecorder.stdout.log') -RedirectStandardError (Join-Path $taskFolder 'WindowRecorder.stderr.log')
                    $taskResult.window_capture.helper_pid=$taskRecorder.Id;$taskResult.window_capture.status='waiting_ready'
                    Save-HighLatencyReport
                }
                if($taskPeer -eq 0){
                    $taskListenDeadline=[DateTime]::UtcNow.AddSeconds([Math]::Min(90,$TimeoutSeconds))
                    do{
                        if($taskProcess.HasExited){throw 'Owned host exited before listening.'}
                        $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern "listening on port $Port\b" -Quiet)
                        if($taskListening){break}
                        Start-Sleep -Milliseconds 250
                    }while([DateTime]::UtcNow -lt $taskListenDeadline)
                    if(-not $taskListening){throw 'Owned host did not start listening before timeout.'}
                }
            }
            if($DryRun){$taskResult.status='dry_run';continue}
            $taskDeadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            while(@($taskProcesses | Where-Object {-not $_.HasExited}).Count -gt 0){
                if([DateTime]::UtcNow -gt $taskDeadline){throw 'Owned fixture timed out.'}
                Start-Sleep -Milliseconds 250
            }
            foreach($taskPeerResult in $taskResult.peers){
                $taskProcess=$taskProcesses[$taskPeerResult.peer]
                $taskProcess.Refresh();$taskPeerResult.exit_code=$taskProcess.ExitCode
                if($taskPeerResult.video_frames_directory){
                    $taskPeerResult.video_frames_written=@(Get-ChildItem -LiteralPath $taskPeerResult.video_frames_directory -Filter '*.png' -File -ErrorAction SilentlyContinue).Count
                    $taskPeerResult.video_timestamps_present=Test-Path -LiteralPath (Join-Path $taskPeerResult.video_frames_directory 'times.csv')
                }
                $taskLines=Get-Content -LiteralPath $taskPeerResult.log
                if($CaptureCorrections -and $taskPeerResult.peer -gt 0){$taskPeerResult.client_corrections=Get-HighLatencyCorrectionStats -Lines $taskLines}
                $taskPeerResult.network_telemetry=Get-HighLatencyNetworkTelemetry -Lines $taskLines
                $taskPeerResult.markers=@($taskLines | Select-String -Pattern $taskDefinition.Marker | ForEach-Object {$_.Line})
                $taskPeerResult.pass=$taskProcess.ExitCode -eq 0 -and $taskPeerResult.markers.Count -gt 0 -and
                    $taskPeerResult.markers[-1] -match '_PASS\b' -and -not ($taskPeerResult.markers -match '_FAIL\b')
                $taskPingLine=$taskLines | Select-String -Pattern 'ping_min=([\d.]+) ping_avg=([\d.]+) ping_max=([\d.]+) ping_samples=(\d+)' | Select-Object -Last 1
                if($taskPingLine -and $taskPingLine.Matches[0].Groups[4].Value -ne '0'){
                    $taskGroups=$taskPingLine.Matches[0].Groups
                    $taskPeerResult.measured_ping=@{source='fixture_aggregate';min_ms=[double]::Parse($taskGroups[1].Value,[Globalization.CultureInfo]::InvariantCulture);avg_ms=[double]::Parse($taskGroups[2].Value,[Globalization.CultureInfo]::InvariantCulture);max_ms=[double]::Parse($taskGroups[3].Value,[Globalization.CultureInfo]::InvariantCulture);samples=[int]$taskGroups[4].Value}
                }
                if($null -eq $taskPeerResult.measured_ping -and $taskPeerResult.network_telemetry){$taskPeerResult.measured_ping=$taskPeerResult.network_telemetry.measured_ping}
            }
            $taskResult.status=if(@($taskResult.peers | Where-Object {-not $_.pass}).Count -eq 0){'passed'}else{'failed'}
        }catch{$taskResult.status='failed';$taskResult.error=$_.Exception.Message}
        finally{
            if($taskRecorder -and -not $taskRecorder.HasExited){'stop' | Set-Content -LiteralPath $taskRecorderStop -Encoding ASCII}
            foreach($taskProcess in $taskProcesses){if(-not $taskProcess.HasExited){Stop-Process -InputObject $taskProcess}}
            if($taskRecorder){
                if(-not $taskRecorder.WaitForExit(30000)){Stop-Process -InputObject $taskRecorder;$taskResult.window_capture.status='helper_shutdown_timeout'}
                if(Test-Path -LiteralPath $taskResult.window_capture.manifest){
                    try{$taskResult.window_capture.result=Get-Content -LiteralPath $taskResult.window_capture.manifest -Raw | ConvertFrom-Json;$taskResult.window_capture.status=$taskResult.window_capture.result.status}
                    catch{$taskResult.window_capture.status='invalid_capture_manifest'}
                }elseif($taskResult.window_capture.status -ne 'helper_shutdown_timeout'){$taskResult.window_capture.status='missing_capture_manifest'}
            }
            Save-HighLatencyReport
        }
        Write-Output "$Scenario $($taskProfile.name): $($taskResult.status)"
    }
    $taskReport.status=if($DryRun){'dry_run'}elseif(@($taskReport.results | Where-Object {$_.status -ne 'passed'}).Count -eq 0){'passed'}else{'failed'}
}finally{
    $env:TEMP=$taskPreviousTemp;$env:TMP=$taskPreviousTmp;$env:UE_SKIP_UBT_SDK_SETUP=$taskPreviousSDKSkip
    $taskReport.finished_utc=[DateTime]::UtcNow.ToString('o');Save-HighLatencyReport
}
Write-Output "Report: $taskReportPath"
if($taskReport.status -eq 'failed'){throw 'High-latency fixture failed; inspect Results.json and peer logs.'}
