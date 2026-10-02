param([ValidateSet('FoodReaction','FoodCollect','FoodBalance','FoodThroat','FoodYawn','FoodSpoil','FoodDamage','FoodStars','FoodCollision')][string[]]$Case=@('FoodReaction','FoodCollect','FoodBalance','FoodThroat','FoodYawn','FoodSpoil','FoodDamage','FoodStars'),[switch]$NullRHI,[ValidatePattern("^[A-Za-z0-9_-]*$")][string]$Revision="")
$ErrorActionPreference='Stop'
# Win64 is already built; avoid a startup SDK query through the engine-wide build lock.
$env:UE_SKIP_UBT_SDK_SETUP='1'
$projectRoot=Split-Path -Parent $PSScriptRoot
$projectFile=Join-Path $projectRoot 'MessControl.uproject'
$engineExe='E:\UE\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$artifactRoot=Join-Path $projectRoot ('Artifacts\FoodRework'+$Revision)
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
function Get-TakeFiles {
    $result=@{source=@{};assets=@{};dllSha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-MessControl.dll')).Hash}
    Get-ChildItem (Join-Path $projectRoot 'Source\MessControl') -File -Recurse | ForEach-Object {$result.source[$_.FullName.Substring($projectRoot.Length+1)]=(Get-FileHash -LiteralPath $_.FullName).Hash}
    $packages=@(Get-ChildItem (Join-Path $projectRoot 'Content\Gameplay\VFX') -File -Recurse)
    $packages+=Get-ChildItem (Join-Path $projectRoot 'Content\FromBlender4') -File -Filter 'SK_Exit.*'
    $packages+=Get-Item (Join-Path $projectRoot 'Content\Maps\L_Mouth.umap')
    # Saved packages actually used by the current menu and its physics profile.
    # Keep capture provenance usable without generated ProjectIndex snapshots.
    $foodMeshNames=@{
        'Art/Meshes/Breakfast'=@('SM_Egg_01','SM_Egg_02','SM_Egg_03')
        'Stylized_Fruits/Meshes'=@('SM_Grape_A','SM_Grape_D','SM_GrapesGreen','SM_GrapesPurple','SM_GrapesStalk')
        'Stylized_Vegetables/Meshes'=@('SM_Broccoli','SM_BroccoliCutA','SM_BroccoliCutB','SM_BroccoliCutC',
            'SM_CabbageGreen','SM_CabbageGreenCutA','SM_CabbageGreenCutB','SM_CabbageRed','SM_CabbageRedCutA','SM_CabbageRedCutB',
            'SM_CarrotACutA','SM_CarrotACutB','SM_CarrotANoLeaves','SM_CarrotBNoLeaves','SM_CarrotSliceA','SM_CarrotSliceB',
            'SM_Chili','SM_ChiliCutA','SM_ChiliCutB','SM_TomatoA','SM_TomatoB','SM_TomatoCutA',
            'SM_TomatoQuartersCutA','SM_TomatoQuartersCutB','SM_TomatoSliceC','SM_TomatoSliceE')
    }
    foreach($folder in $foodMeshNames.Keys) {
        foreach($meshName in $foodMeshNames[$folder]) {$packages+=Get-Item -LiteralPath (Join-Path $projectRoot ('Content/'+$folder+'/'+$meshName+'.uasset'))}
    }
    foreach($foodDataName in @('DT_BreakfastMenu','DA_FoodPhysics')) {$packages+=Get-Item -LiteralPath (Join-Path $projectRoot ('Content/Data/'+$foodDataName+'.uasset'))}
    $packages | ForEach-Object {$result.assets[$_.FullName.Substring($projectRoot.Length+1)]=(Get-FileHash -LiteralPath $_.FullName).Hash}
    return $result
}
foreach($item in $Case) {
    $captureName=$item+$Revision
    $frameRoot=Join-Path $projectRoot "Saved\ApprovalFrames\$captureName"
    if(Test-Path -LiteralPath $frameRoot) {throw "Frames already exist for $item; preserve them before another take."}
    $logFile=Join-Path $projectRoot "Saved\Logs\FoodRework_$captureName.log"
    $captureArguments=@(('"'+$projectFile+'"'),'/Game/Maps/L_Mouth?Seed=41','-game','-MCLegacyDays','-unattended','-nosound','-nosplash','-nop4','-NoLiveCoding',"-MCFoodRework=$item",('-abslog="'+$logFile+'"'))
    if($item -eq 'FoodCollision') {$captureArguments=$captureArguments | Where-Object {$_ -notlike '-MCFoodRework=*'};$captureArguments+='-MCFoodCollision'}
    if($NullRHI) {$captureArguments+='-nullrhi'} else {
        if((Get-PSDrive E).Free -lt 400MB) {throw 'Insufficient space for a render take.'}
        $captureArguments+=@("-MCVideo=$captureName",'-MCCaptureTimeScale=.25','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','-ExecCmds="t.MaxFPS 30,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 2,sg.ShadowQuality 2,r.Streaming.PoolSize 512,r.ScreenPercentage 100"')
    }
    $takeStart=Get-TakeFiles
    $captureProcess=Start-Process -FilePath $engineExe -ArgumentList $captureArguments -WindowStyle Hidden -PassThru
    try {
        $deadline=[DateTime]::Now.AddSeconds(240)
        while(-not $captureProcess.HasExited -and [DateTime]::Now -lt $deadline) {Start-Sleep -Milliseconds 500;$captureProcess.Refresh()}
        if(-not $captureProcess.HasExited) {throw "$item timed out"}
        $logText=Get-Content -LiteralPath $logFile -Raw
        $checks=$logText -split '\r?\n' | Where-Object {$_ -match 'MC_FOOD_REWORK_CHECK|MC_FOOD_COLLISION_|MC_VALIDATION_|MC_FOOD_REWORK_START'}
        $checks | Set-Content -LiteralPath (Join-Path $artifactRoot ($captureName+'_Validation.txt')) -Encoding utf8
        $expectedValidation=if($item -eq 'FoodCollision') {'MC_VALIDATION_PASS FOOD_COLLISION'} else {"MC_VALIDATION_PASS FOOD_REWORK_$item"}
        if($captureProcess.ExitCode -ne 0 -or $logText -match 'MC_FOOD_REWORK_CHECK .* FAIL|MC_FOOD_COLLISION_CHECK FAIL|MC_VALIDATION_FAIL' -or $logText -notmatch $expectedValidation) {throw "$item failed. See $logFile"}
        if(-not $NullRHI) {
            $takeEnd=Get-TakeFiles
            if($takeStart.dllSha256 -ne $takeEnd.dllSha256) {throw 'Native binary changed during the take'}
            foreach($group in @('source','assets')) {
                if($takeStart[$group].Count -ne $takeEnd[$group].Count) {throw "$group files changed during the take"}
                foreach($path in $takeStart[$group].Keys) {if($takeStart[$group][$path] -ne $takeEnd[$group][$path]) {throw "File changed during the take: $path"}}
            }
            $fingerprint=@{case=$captureName;scenario=$item;revision=$Revision;validation=@{sha256=(Get-FileHash -LiteralPath (Join-Path $artifactRoot ($captureName+'_Validation.txt'))).Hash.ToLower()};map='/Game/Maps/L_Mouth';engine='Unreal 5.8.1';capture='Unreal offscreen game render target';width=1280;height=720;dllSha256=$takeEnd.dllSha256;source=$takeEnd.source;assets=$takeEnd.assets;filesUnchangedDuringTake=$true}
            $fingerprint | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $frameRoot 'Capture_Manifest.json') -Encoding utf8
            Copy-Item -LiteralPath (Join-Path $artifactRoot ($captureName+'_Validation.txt')) -Destination (Join-Path $projectRoot ('Artifacts\Approval\'+$captureName+'_Validation.txt'))
            & C:\Python314\python.exe (Join-Path $PSScriptRoot 'encode_approval.py') $captureName $item
            if($LASTEXITCODE -ne 0) {throw "$item encode failed"}
            foreach($suffix in @('.mp4','.png','_Recording.json')) {Move-Item -LiteralPath (Join-Path $projectRoot ('Artifacts\Approval\'+$captureName+$suffix)) -Destination (Join-Path $artifactRoot ($captureName+$suffix))}
            $archiveRoot='D:\CodexCaptureCache\MessControl_FoodRework_20261002'
            New-Item -ItemType Directory -Path $archiveRoot -Force | Out-Null
            $resolvedFrames=(Resolve-Path -LiteralPath $frameRoot).Path
            $allowedFrames=Join-Path $projectRoot 'Saved\ApprovalFrames'
            if(-not $resolvedFrames.StartsWith($allowedFrames+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid frame path'}
            $archiveDestination=Join-Path $archiveRoot $captureName
            if(Test-Path -LiteralPath $archiveDestination) {$archiveDestination+='_'+(Get-Date -Format 'HHmmss')}
            Move-Item -LiteralPath $resolvedFrames -Destination $archiveDestination
        }
        Write-Output "$item PASS"
    } finally {if(-not $captureProcess.HasExited) {Stop-Process -Id $captureProcess.Id -Force}}
}
