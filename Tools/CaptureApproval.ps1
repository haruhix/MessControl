param([ValidateSet('Climb','Coffee','Cola','Camera','Ulcer','Tools','Brush','Hazards','Grip','Throat','Materials','Pickaxe')][string]$Case='Climb',[switch]$NullRHI,[switch]$EncodeOnly)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskDir=Join-Path $taskRoot "Saved/ApprovalFrames/$Case"
$taskOut=Join-Path $taskRoot 'Artifacts/Approval'
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
if(-not $EncodeOnly) {
    if((Get-PSDrive -Name E).Free -lt 1.2GB){throw 'Recording needs at least 1.2 GB of free space on E'}
    # A dated run folder preserves earlier evidence and avoids recursive deletion.
    if(Test-Path -LiteralPath $taskDir) { Move-Item -LiteralPath $taskDir -Destination ($taskDir+'_'+(Get-Date -Format 'yyyyMMdd_HHmmss')) }
    $taskFlags=switch($Case) {
        Ulcer {@('-MCUlcerReworkTest','-MCUlcerCapture')}
        Tools {@('-MCInventoryTest','-MCInventoryCapture')}
        Brush {@('-MCBrushTest','-MCBrushFacing','-MCBrushCapture')}
        Hazards {@('-MCGameplayV3Test','-MCGameplayV3Capture')}
        Grip {@('-MCGripTest','-MCGripCapture')}
        Throat {@('-MCThroatTest','-MCThroatCapture','-MCExpectedPlayers=1')}
        default {@("-MCApproval=$Case")}
    }
    $taskLog=Join-Path $taskRoot "Saved/Logs/Approval_$Case.log"
    $taskMap=if($Case -eq 'Grip'){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'/Game/Maps/L_Mouth?Seed=41'}
    $taskArgs=@("`"$taskRoot/MessControl.uproject`"",$taskMap,'-game','-MCLegacyDays','-unattended','-nosound','-nosplash','-nop4','-NoLiveCoding',"`"-abslog=$taskLog`"",'-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=30')+$taskFlags
    if($NullRHI) { $taskArgs+=@('-nullrhi','"-ExecCmds=t.MaxFPS 30"') }
    else { $taskArgs+=@("-MCVideo=$Case",'-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','"-ExecCmds=t.MaxFPS 20,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 2,sg.ShadowQuality 2,r.SSR.Quality 2,r.SSR.MaxRoughness .65,r.Streaming.PoolSize 512,r.ScreenPercentage 100"') }
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
        if(-not $taskProc.WaitForExit(240000)) {throw "Approval $Case timed out: $taskLog"}
        $taskChecks=Select-String -LiteralPath $taskLog -Pattern 'MC_APPROVAL_CHECK|MC_ULCER_(CHECK|PASS|FAIL)|MC_VALIDATION_(PASS|FAIL)|MC_INVENTORY_(CHECK|PASS|FAIL)|MC_BRUSH_(PASS|FAIL)|MC_BRUSH_FACING_(CASE|PASS|FAIL)|MC_PICKAXE_CLEARANCE|MC_UVULA_ANIMATION'
        $taskChecks.Line | Set-Content -LiteralPath (Join-Path $taskOut ($Case+'_Validation.txt')) -Encoding utf8
        if($taskProc.ExitCode -ne 0 -or -not ($taskChecks.Line -match 'MC_(VALIDATION_PASS|ULCER_PASS|INVENTORY_PASS|BRUSH_FACING_PASS)')) {throw "Approval $Case failed: $taskLog"}
        for($taskIndex=0;$taskIndex -lt $taskClients.Count;$taskIndex++) {
            if(-not $taskClients[$taskIndex].WaitForExit(60000)){throw 'Grip client timed out'}
            $taskClientLog=Join-Path $taskRoot "Saved/Logs/Approval_GripClient$($taskIndex+1).log"
            $taskClientCheck=Select-String -LiteralPath $taskClientLog -Pattern 'MC_VALIDATION_(PASS|FAIL) GRIP' | Select-Object -Last 1
            if($taskClients[$taskIndex].ExitCode -ne 0 -or $taskClientCheck.Line -notmatch 'MC_VALIDATION_PASS'){throw "Grip client failed: $taskClientLog"}
            $taskClientCheck.Line | Add-Content -LiteralPath (Join-Path $taskOut 'Grip_Validation.txt') -Encoding utf8
        }
    } finally {foreach($taskCleanup in @($taskProc)+$taskClients){if(-not $taskCleanup.HasExited) {Stop-Process -Id $taskCleanup.Id}}}
}
if(-not $NullRHI) {
    & C:/Python314/python.exe "$taskRoot/Tools/encode_approval.py" $Case
    if($LASTEXITCODE -ne 0) {throw "Encoding $Case failed"}
}
