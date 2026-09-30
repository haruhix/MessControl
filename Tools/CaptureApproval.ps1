param(
    [ValidateSet('Climb','Coffee','Cola','Camera','Ulcer','Tools','Brush','Hazards','Grip','Throat','Materials','Pickaxe','ShiftReset','SprayNetwork')][string]$Case='Climb',
    [switch]$NullRHI,[switch]$EncodeOnly,
    [ValidateRange(0,250)][int]$PacketLagMs=0,
    [ValidateRange(0,10)][int]$PacketLoss=0
)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'Saved/StopFinalCapture.flag')) {throw 'Capture queue stopped for fixes'}
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskDir=Join-Path $taskRoot "Saved/ApprovalFrames/$Case"
$taskOut=Join-Path $taskRoot 'Artifacts/Approval'
function Get-TaskTreeDigest([string]$taskSubdirectory,[switch]$SavedPackages) {
    $taskPaths=[System.Collections.Generic.List[string]]::new()
    foreach($taskFile in Get-ChildItem -LiteralPath (Join-Path $taskRoot $taskSubdirectory) -Recurse -File) {
        $taskRelative=$taskFile.FullName.Substring($taskRoot.Length+1).Replace('\','/')
        if($SavedPackages -and ($taskFile.Extension -notin '.uasset','.umap' -or $taskRelative.StartsWith('Content/_CodexMapMerge/'))) {continue}
        $taskPaths.Add($taskRelative)
    }
    $taskPaths.Sort([System.StringComparer]::Ordinal)
    $taskDigest=[System.Security.Cryptography.SHA256]::Create()
    [long]$taskBytes=0
    try {
        foreach($taskRelative in $taskPaths) {
            $taskFilePath=Join-Path $taskRoot $taskRelative
            $taskHash=(Get-FileHash -LiteralPath $taskFilePath -Algorithm SHA256).Hash.ToLowerInvariant()
            $taskBytes+=(Get-Item -LiteralPath $taskFilePath).Length
            $taskRecord=[System.Text.Encoding]::UTF8.GetBytes($taskRelative+"`0"+$taskHash+"`n")
            [void]$taskDigest.TransformBlock($taskRecord,0,$taskRecord.Length,$taskRecord,0)
        }
        [void]$taskDigest.TransformFinalBlock([byte[]]::new(0),0,0)
        return [ordered]@{algorithm='sha256-tree-v1';root=$taskSubdirectory;fileCount=$taskPaths.Count;bytes=$taskBytes;sha256=([System.BitConverter]::ToString($taskDigest.Hash)).Replace('-','').ToLowerInvariant()}
    } finally {$taskDigest.Dispose()}
}
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
if(-not $EncodeOnly) {
    if((Get-PSDrive -Name E).Free -lt 1.2GB){throw 'Recording needs at least 1.2 GB of free space on E'}
    # Check absolute archive paths before moving an existing recording folder.
    $taskFramesRoot=[System.IO.Path]::GetFullPath((Join-Path $taskRoot 'Saved/ApprovalFrames'))+[System.IO.Path]::DirectorySeparatorChar
    $taskDir=[System.IO.Path]::GetFullPath($taskDir)
    $taskArchive=$taskDir+'_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')
    foreach($taskPath in @($taskDir,$taskArchive)) {
        if(-not $taskPath.StartsWith($taskFramesRoot,[System.StringComparison]::OrdinalIgnoreCase)) {throw "Recording path is outside ApprovalFrames: $taskPath"}
    }
    # A dated run folder preserves earlier evidence and avoids recursive deletion.
    if(Test-Path -LiteralPath $taskDir) { Move-Item -LiteralPath $taskDir -Destination $taskArchive }
    New-Item -ItemType Directory -Path $taskDir -Force | Out-Null
    # UnrealEditor-Cmd -game loads the project's Editor module, not MessControl.exe.
    $taskDll=Join-Path $taskRoot 'Binaries/Win64/UnrealEditor-MessControl.dll'
    if(-not (Test-Path -LiteralPath $taskDll)) {throw "Build the Editor target before recording: $taskDll"}
    $taskCaptureManifest=[ordered]@{
        schemaVersion=1;case=$Case;startedUtc=$null;completedUtc=$null
        captureMode=$(if($NullRHI){'nullrhi validation'}else{'Unreal offscreen game render target'})
        editorDll=[ordered]@{path='Binaries/Win64/UnrealEditor-MessControl.dll';sha256=(Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash.ToLowerInvariant()}
        source=(Get-TaskTreeDigest 'Source/MessControl')
        savedContent=(Get-TaskTreeDigest 'Content' -SavedPackages)
        contentExclusions=@('Content/_CodexMapMerge/**');contentExtensions=@('.uasset','.umap')
        render=$(if($NullRHI){$null}else{[ordered]@{width=1280;height=720;maxFPS=20;timeScale=$(if($Case -in 'Grip','SprayNetwork'){1.0}else{0.25})}})
    }
    if($Case -eq 'SprayNetwork') {
        $taskCaptureManifest.network=[ordered]@{mode='SprayNetwork';players=4;hostPeer=0;renderedPeer=$(if($NullRHI){$null}else{1});packetLagMs=$PacketLagMs;packetLossPercent=$PacketLoss;timeScale=1.0}
        # Test.ps1 owns all four processes; only the remote owning client renders.
        # Clear every peer log first so an early startup failure cannot reuse one.
        for($taskIndex=0;$taskIndex -lt 4;$taskIndex++) {
            $taskPeerLog=Join-Path $taskRoot "Saved/Logs/SprayNetwork$taskIndex.log"
            if(Test-Path -LiteralPath $taskPeerLog) {Remove-Item -LiteralPath $taskPeerLog}
        }
        $taskCaptureManifest.startedUtc=[DateTime]::UtcNow.ToString('o')
        $taskCaptureManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskDir 'Capture_Manifest.json') -Encoding utf8
        $taskNetworkError=$null
        try {
            & "$PSScriptRoot/Test.ps1" -EngineRoot 'E:/UE/UE_5.8' -Mode SprayNetwork -PacketLagMs $PacketLagMs -PacketLoss $PacketLoss -CaptureSpray:(!$NullRHI)
        } catch {$taskNetworkError=$_}
        $taskValidationPath=Join-Path $taskOut 'SprayNetwork_Validation.txt'
        $taskNetworkLines=[System.Collections.Generic.List[string]]::new()
        $taskPeerPasses=0
        for($taskIndex=0;$taskIndex -lt 4;$taskIndex++) {
            $taskPeerLog=Join-Path $taskRoot "Saved/Logs/SprayNetwork$taskIndex.log"
            $taskNetworkLines.Add("# peer=$taskIndex log=Saved/Logs/SprayNetwork$taskIndex.log")
            if(-not (Test-Path -LiteralPath $taskPeerLog)) {continue}
            $taskPeerChecks=@(Select-String -LiteralPath $taskPeerLog -Pattern 'MC_SPRAY_NETWORK|MC_VALIDATION_(PASS|FAIL) SPRAY_NETWORK')
            foreach($taskCheck in $taskPeerChecks) {$taskNetworkLines.Add($taskCheck.Line)}
            $taskPeerFinal=$taskPeerChecks | Where-Object {$_.Line -match 'MC_VALIDATION_(PASS|FAIL) SPRAY_NETWORK'} | Select-Object -Last 1
            if($taskPeerFinal.Line -match 'MC_VALIDATION_PASS SPRAY_NETWORK' -and $taskPeerFinal.Line -match 'invalid=0\b' -and -not ($taskPeerChecks.Line -match 'MC_VALIDATION_FAIL')) {
                if($NullRHI -or $taskIndex -ne 1 -or $taskPeerFinal.Line -match 'rendered_activation=1\b') {$taskPeerPasses++}
            }
        }
        $taskNetworkLines | Set-Content -LiteralPath $taskValidationPath -Encoding utf8
        if($taskNetworkError) {throw $taskNetworkError}
        if($taskPeerPasses -ne 4) {throw "SprayNetwork needs four PASS results and rendered client activation: $taskValidationPath"}
        $taskCaptureManifest.completedUtc=[DateTime]::UtcNow.ToString('o')
        $taskCaptureManifest.validation=[ordered]@{result='PASS';successfulProcessRuns=$taskPeerPasses;sha256=(Get-FileHash -LiteralPath $taskValidationPath -Algorithm SHA256).Hash.ToLowerInvariant()}
        $taskCaptureManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskDir 'Capture_Manifest.json') -Encoding utf8
    } else {
    $taskFlags=switch($Case) {
        Ulcer {@('-MCUlcerReworkTest','-MCUlcerCapture')}
        Tools {@('-MCInventoryTest','-MCInventoryCapture')}
        Brush {@('-MCBrushTest','-MCBrushFacing','-MCBrushCapture')}
        Hazards {@('-MCGameplayV3Test','-MCGameplayV3Capture')}
        Grip {@('-MCGripTest','-MCGripCapture')}
        ShiftReset {@('-MCShiftResetTest')}
        Throat {@('-MCThroatTest','-MCThroatCapture','-MCExpectedPlayers=1')}
        default {@("-MCApproval=$Case")}
    }
    $taskLog=Join-Path $taskRoot "Saved/Logs/Approval_$Case.log"
    $taskMap=if($Case -eq 'Grip'){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'/Game/Maps/L_Mouth?Seed=41'}
    $taskArgs=@("`"$taskRoot/MessControl.uproject`"",$taskMap,'-game','-MCLegacyDays','-unattended','-nosound','-nosplash','-nop4','-NoLiveCoding',"`"-abslog=$taskLog`"",'-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=30')+$taskFlags
    if($NullRHI) { $taskArgs+=@('-nullrhi','"-ExecCmds=t.MaxFPS 30"') }
    else {
        $taskArgs+=@("-MCVideo=$Case",'-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','"-ExecCmds=t.MaxFPS 20,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 2,sg.ShadowQuality 2,r.SSR.Quality 2,r.SSR.MaxRoughness .65,r.Streaming.PoolSize 512,r.ScreenPercentage 100"')
        if($Case -ne 'Grip') {$taskArgs+='-MCCaptureTimeScale=0.25'}
    }
    $taskCaptureManifest.startedUtc=[DateTime]::UtcNow.ToString('o')
    $taskCaptureManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskDir 'Capture_Manifest.json') -Encoding utf8
    $taskProc=Start-Process 'E:/UE/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -WindowStyle Hidden -ArgumentList $taskArgs -PassThru
    $taskClients=@()
    try {
        if($Case -eq 'Grip') {
            $taskDeadline=(Get-Date).AddSeconds(60)
            do {Start-Sleep -Milliseconds 500; if($taskProc.HasExited){throw 'Grip host exited before listening'}} until(((Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern 'listening on port 7777' -Quiet)) -or (Get-Date) -gt $taskDeadline)
            if((Get-Date) -gt $taskDeadline){throw 'Grip host did not start listening'}
            for($taskIndex=1;$taskIndex -le 3;$taskIndex++) {
                $taskClientLog=Join-Path $taskRoot "Saved/Logs/Approval_GripClient$taskIndex.log"
                $taskClientArgs=@("`"$taskRoot/MessControl.uproject`"",'127.0.0.1:7777','-game','-MCLegacyDays','-MCGripTest','-nullrhi','-unattended','-nosound','-nosplash','-nop4','-NoLiveCoding',"`"-abslog=$taskClientLog`"",'"-ExecCmds=t.MaxFPS 30"')
                $taskClients+=Start-Process 'E:/UE/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -WindowStyle Hidden -ArgumentList $taskClientArgs -PassThru
            }
        }
        if(-not $taskProc.WaitForExit($(if($Case -eq 'ShiftReset'){360000}else{240000}))) {throw "Approval $Case timed out: $taskLog"}
        $taskChecks=Select-String -LiteralPath $taskLog -Pattern 'MC_APPROVAL_CHECK|MC_SHIFT_RESET_CHECK|MC_ULCER_(CHECK|PASS|FAIL)|MC_VALIDATION_(PASS|FAIL)|MC_INVENTORY_(CHECK|PASS|FAIL)|MC_BRUSH_(PASS|FAIL)|MC_BRUSH_FACING_(CASE|PASS|FAIL)|MC_PICKAXE_(ENAMEL_)?CLEARANCE|MC_UVULA_ANIMATION'
        $taskChecks.Line | Set-Content -LiteralPath (Join-Path $taskOut ($Case+'_Validation.txt')) -Encoding utf8
        if($taskProc.ExitCode -ne 0 -or ($taskChecks.Line -match 'MC_\w+_FAIL\b|MC_\w+_CHECK FAIL\b') -or -not ($taskChecks.Line -match 'MC_(VALIDATION_PASS|ULCER_PASS|INVENTORY_PASS|BRUSH_FACING_PASS)')) {throw "Approval $Case failed: $taskLog"}
        for($taskIndex=0;$taskIndex -lt $taskClients.Count;$taskIndex++) {
            if(-not $taskClients[$taskIndex].WaitForExit(60000)){throw 'Grip client timed out'}
            $taskClientLog=Join-Path $taskRoot "Saved/Logs/Approval_GripClient$($taskIndex+1).log"
            $taskClientCheck=Select-String -LiteralPath $taskClientLog -Pattern 'MC_VALIDATION_(PASS|FAIL) GRIP' | Select-Object -Last 1
            if($taskClients[$taskIndex].ExitCode -ne 0 -or $taskClientCheck.Line -notmatch 'MC_VALIDATION_PASS'){throw "Grip client failed: $taskClientLog"}
            $taskClientCheck.Line | Add-Content -LiteralPath (Join-Path $taskOut 'Grip_Validation.txt') -Encoding utf8
        }
        $taskValidationPath=Join-Path $taskOut ($Case+'_Validation.txt')
        $taskCaptureManifest.completedUtc=[DateTime]::UtcNow.ToString('o')
        $taskCaptureManifest.validation=[ordered]@{result='PASS';sha256=(Get-FileHash -LiteralPath $taskValidationPath -Algorithm SHA256).Hash.ToLowerInvariant()}
        $taskCaptureManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskDir 'Capture_Manifest.json') -Encoding utf8
    } finally {foreach($taskCleanup in @($taskProc)+$taskClients){if(-not $taskCleanup.HasExited) {Stop-Process -Id $taskCleanup.Id}}}
    }
}
if(-not $NullRHI) {
    & C:/Python314/python.exe "$taskRoot/Tools/encode_approval.py" $Case
    if($LASTEXITCODE -ne 0) {throw "Encoding $Case failed"}
}
