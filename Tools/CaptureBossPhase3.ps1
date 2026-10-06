param(
    [string]$EngineRoot='E:/UE/UE_5.8',
    [string]$Python='C:/Python314/python.exe',
    [ValidateRange(.25,1)][double]$TimeScale=.25,
    [switch]$EncodeOnly
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskFramesRoot=[IO.Path]::GetFullPath((Join-Path $taskRoot 'Saved/ApprovalFrames'))
$taskDir=[IO.Path]::GetFullPath((Join-Path $taskFramesRoot 'BossPhase3'))
$taskOut=Join-Path $taskRoot 'Artifacts/Approval'
$taskLog=Join-Path $taskRoot 'Saved/Logs/BossPhase3.log'
$taskValidation=Join-Path $taskOut 'BossPhase3_Validation.txt'
$taskCulture=[Globalization.CultureInfo]::InvariantCulture
$taskManifestPath=Join-Path $taskDir 'Capture_Manifest.json'
if (-not (Test-Path -LiteralPath $Python)) { throw "Pass -Python with a usable Python runtime: $Python" }
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
if (-not $EncodeOnly) {
    $taskEditor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $taskDll=Join-Path $taskRoot 'Binaries/Win64/UnrealEditor-MessControl.dll'
    foreach ($taskRequired in @($taskEditor,$taskDll)) {
        if (-not (Test-Path -LiteralPath $taskRequired)) { throw "Missing editor executable/module: $taskRequired" }
    }
    # Verify both resolved paths before archiving only this scenario's own folder.
    $taskArchive=$taskDir+'_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')
    $taskBoundary=$taskFramesRoot+[IO.Path]::DirectorySeparatorChar
    foreach ($taskPath in @($taskDir,$taskArchive)) {
        if (-not $taskPath.StartsWith($taskBoundary,[StringComparison]::OrdinalIgnoreCase)) {
            throw "Recording folder is outside this workspace's ApprovalFrames: $taskPath"
        }
    }
    if (Test-Path -LiteralPath $taskDir) {
        $taskOld=Get-Item -LiteralPath $taskDir
        if ($taskOld.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'The BossPhase3 frame directory must not be a link or junction.' }
        Move-Item -LiteralPath $taskDir -Destination $taskArchive
    }
    New-Item -ItemType Directory -Path $taskDir -Force | Out-Null
    New-Item -ItemType Directory -Path (Split-Path -Parent $taskLog) -Force | Out-Null
    if (Test-Path -LiteralPath $taskLog) { Remove-Item -LiteralPath $taskLog }
    $taskManifest=[ordered]@{
        schemaVersion=1;case='BossPhase3';startedUtc=[DateTime]::UtcNow.ToString('o');completedUtc=$null
        captureMode='Unreal offscreen game render target'
        editorDll=[ordered]@{path='Binaries/Win64/UnrealEditor-MessControl.dll';sha256=(Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash.ToLowerInvariant()}
        render=[ordered]@{width=1280;height=720;maxFPS=30;sampleFPS=30;timeScale=$TimeScale}
        review=[ordered]@{flag='MCBossPhase3Review';clips=@('Idle','Walk','PunchLeft','PunchRight','Kick','Hurt','Death','Roar');phase1Isolation=$true}
    }
    $taskManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskManifestPath -Encoding utf8
    $taskScale=$TimeScale.ToString('0.##',$taskCulture)
    $taskArgs=@(
        "`"$taskRoot/MessControl.uproject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-MCLegacyDays','-nosteam',
        '-MCRoguelikePreview','-MCBossPhase3Review','-MCExpectedPlayers=1','-MCVideo=BossPhase3',
        '-MCVideoFPS=30','-MCVideoStartSeconds=3.7',"-MCCaptureTimeScale=$taskScale",
        '-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-unattended',
        '-nosound','-nosplash','-nop4','-NoLiveCoding','-NoScreenMessages',"`"-abslog=$taskLog`"",
        '"-ExecCmds=t.MaxFPS 30,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 2,sg.ShadowQuality 2,r.ScreenPercentage 100"'
    )
    $taskProcess=Start-Process -FilePath $taskEditor -WindowStyle Hidden -ArgumentList $taskArgs -PassThru
    try {
        # Poll in short intervals; shaders/asset loading can extend startup.
        $taskDeadline=[DateTime]::UtcNow.AddMinutes(15)
        while (-not $taskProcess.HasExited -and [DateTime]::UtcNow -lt $taskDeadline) {
            [void]$taskProcess.WaitForExit(1000)
        }
        if (-not $taskProcess.HasExited) { throw "Phase 3 recording timed out: $taskLog" }
        $taskChecks=@(Select-String -LiteralPath $taskLog -Pattern 'MC_BOSS_PHASE3_(CLIP|ISOLATION|CAMERA|CHECK|PASS|FAIL)')
        $taskChecks.Line | Set-Content -LiteralPath $taskValidation -Encoding utf8
        if ($taskProcess.ExitCode -ne 0 -or ($taskChecks.Line -match 'MC_BOSS_PHASE3_FAIL') -or -not ($taskChecks.Line -match 'MC_BOSS_PHASE3_PASS')) {
            throw "Phase 3 review failed; encoding was skipped. Inspect $taskLog"
        }
        if (Select-String -LiteralPath $taskLog -Pattern 'Failed to compile Material|LogMaterial: Error|LogShaderCompilers: Error' -Quiet) {
            throw "A material/shader failed during review: $taskLog"
        }
        $taskManifest.completedUtc=[DateTime]::UtcNow.ToString('o')
        $taskManifest.validation=[ordered]@{result='PASS';sha256=(Get-FileHash -LiteralPath $taskValidation -Algorithm SHA256).Hash.ToLowerInvariant()}
        $taskManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskManifestPath -Encoding utf8
    } finally {
        if (-not $taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id }
    }
}
if (-not (Test-Path -LiteralPath $taskValidation) -or -not (Select-String -LiteralPath $taskValidation -Pattern 'MC_BOSS_PHASE3_PASS' -Quiet) -or
    (Select-String -LiteralPath $taskValidation -Pattern 'MC_BOSS_PHASE3_FAIL' -Quiet)) {
    throw 'Encoding requires a successful phase 3 review for these frames.'
}
$taskManifest=Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
if ($taskManifest.case -ne 'BossPhase3' -or $taskManifest.validation.result -ne 'PASS' -or
    $taskManifest.validation.sha256 -ne (Get-FileHash -LiteralPath $taskValidation -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'Capture manifest and PASS evidence do not match.'
}
$taskClips=@(Get-Content -LiteralPath $taskValidation | ForEach-Object {
    if ($_ -match 'MC_BOSS_PHASE3_CLIP index=(\d+) name=(\w+) start=([\d.]+) length=([\d.]+) hold=([\d.]+)') {
        [PSCustomObject]@{Index=[int]$Matches[1];Name=$Matches[2];Start=[double]::Parse($Matches[3],$taskCulture);Length=[double]::Parse($Matches[4],$taskCulture)}
    }
})
if ($taskClips.Count -ne 8 -or @($taskClips.Index | Sort-Object -Unique).Count -ne 8) { throw 'Review did not demonstrate all eight F3 clips.' }
$taskRows=@(Get-Content -LiteralPath (Join-Path $taskDir 'times.csv') | Where-Object { $_ } | ForEach-Object {
    $taskParts=$_ -split ','
    if (Test-Path -LiteralPath (Join-Path $taskDir $taskParts[0])) {
        [PSCustomObject]@{Name=$taskParts[0];Time=[double]::Parse($taskParts[1],$taskCulture)}
    }
})
# Remove startup from the encode list; keep the original frames and timing as evidence.
$taskOriginal=Join-Path $taskDir 'times_original.csv'
if (-not (Test-Path -LiteralPath $taskOriginal)) { Copy-Item -LiteralPath (Join-Path $taskDir 'times.csv') -Destination $taskOriginal }
$taskRows=@($taskRows | Where-Object { $_.Time -ge ($taskClips[0].Start-.15) })
if ($taskRows.Count -lt 30) { throw 'Insufficient timed game-render frames.' }
$taskRows | ForEach-Object { $_.Name+','+$_.Time.ToString('F6',$taskCulture) } | Set-Content -LiteralPath (Join-Path $taskDir 'times.csv') -Encoding utf8
$taskStills=Join-Path $taskRoot 'Saved/RogueReview/BossPhase3'
New-Item -ItemType Directory -Path $taskStills -Force | Out-Null
foreach ($taskClip in $taskClips) {
    $taskPoseAt=$taskClip.Start+[Math]::Min($taskClip.Length*.55,2.)
    $taskNearest=$taskRows | Sort-Object { [Math]::Abs($_.Time-$taskPoseAt) } | Select-Object -First 1
    Copy-Item -LiteralPath (Join-Path $taskDir $taskNearest.Name) -Destination (Join-Path $taskStills ('Clip{0:D2}_{1}.png' -f $taskClip.Index,$taskClip.Name)) -Force
}
& $Python (Join-Path $taskRoot 'Tools/encode_approval.py') 'BossPhase3'
if ($LASTEXITCODE -ne 0) { throw 'Phase 3 video encoding failed.' }
Write-Output "Phase 3: $($taskRows.Count) timed Unreal frames, eight F3 clips; $taskOut/BossPhase3.mp4"
