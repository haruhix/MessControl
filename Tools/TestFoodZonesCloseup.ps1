param([string]$EngineRoot=$env:UE_ROOT,[switch]$HideThroatArt,[ValidateRange(0,240)][int]$MaxFPS=30)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw -LiteralPath 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProject=Join-Path $taskRoot 'MessControl.uproject'
$taskLog=Join-Path $taskRoot $(if($HideThroatArt){'Saved\Logs\FoodZonesCloseupHideThroatArt.log'}else{'Saved\Logs\FoodZonesCloseup.log'})
$taskCaptureFolder=Join-Path $taskRoot 'Saved\FoodZonesCloseup'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $taskLog),$taskCaptureFolder | Out-Null
if($HideThroatArt) {
    $taskBeforeHide=Join-Path $taskRoot 'Saved\FoodZonesCloseupBeforeHide'
    New-Item -ItemType Directory -Force -Path $taskBeforeHide | Out-Null
    foreach($taskExistingCapture in Get-ChildItem -LiteralPath $taskCaptureFolder -File -Filter '*.png') {
        Copy-Item -LiteralPath $taskExistingCapture.FullName -Destination (Join-Path $taskBeforeHide $taskExistingCapture.Name) -Force
    }
}
if(Test-Path -LiteralPath $taskLog) {Remove-Item -LiteralPath $taskLog}
$taskArgs=@("`"$taskProject`"",'/Game/Maps/L_Mouth?Seed=41','-game','-nosteam','-MCFoodZonesCloseup','-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"",
    '-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages',
    "-ExecCmds=`"t.MaxFPS $MaxFPS,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 1,sg.ReflectionQuality 1,sg.ShadowQuality 1,sg.PostProcessQuality 1,r.ScreenPercentage 100`"")
if($HideThroatArt) {$taskArgs+='-MCFoodZonesHideThroatArt'}
$taskProcess=$null
try {
    $taskProcess=Start-Process -FilePath $taskEditor -WindowStyle Hidden -PassThru -ArgumentList $taskArgs
    if(-not $taskProcess.WaitForExit(240000)) {throw "Delivery closeup render timed out. See $taskLog"}
    if($taskProcess.ExitCode -ne 0 -or -not (Select-String -LiteralPath $taskLog -Pattern 'MC_FOOD_ZONES_CLOSEUP_PASS' -Quiet) -or (Select-String -LiteralPath $taskLog -Pattern 'MC_FOOD_ZONES_FAIL' -Quiet)) {
        throw "Delivery closeup geometry/render run failed. See $taskLog"
    }
    $taskExpected=@()
    foreach($taskView in 0..3) {
        $taskRole=if($taskView -lt 2){'Green'}else{'Red'}
        $taskAngle=if(($taskView%2) -eq 1){'RimGrazing'}else{'CloseOblique'}
        foreach($taskPhase in 'Idle','PlayerPressure','WaveCrestNear','WaveCrestRim') {
            $taskExpected+=Join-Path $taskCaptureFolder ('{0:00}_{1}_{2}_{3}.png' -f $taskView,$taskRole,$taskAngle,$taskPhase)
        }
    }
    foreach($taskCapture in $taskExpected) {
        if(-not (Test-Path -LiteralPath $taskCapture) -or (Get-Item -LiteralPath $taskCapture).LastWriteTimeUtc -lt $taskProcess.StartTime.ToUniversalTime()) {
            throw "Closeup capture missing or stale: $taskCapture"
        }
    }
    Write-Output "PASS: sixteen fresh closeup captures, native triangle attachment and upper-surface coverage under idle, player pressure and directional waves. Inspect the rendered images in $taskCaptureFolder"
} finally {
    if($taskProcess -and -not $taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id}
}
