param(
    [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
    [string]$BuildRoot,
    [string]$ReportDirectory,
    [int[]]$FrameRates=@(30,120)
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $ReportDirectory){$ReportDirectory=Join-Path $taskRoot 'Saved/GamepadValidation'}
New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
$taskExe=if($BuildRoot){Join-Path $BuildRoot 'MessControl/Binaries/Win64/MessControl.exe'}else{Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
if(-not(Test-Path -LiteralPath $taskExe)){throw "Executable missing: $taskExe"}
$taskCases=@(@{Name='Menu';FPS=60;Map='/Game/Maps/L_MainMenu';Flag='-MCGamepadMenuTest'})
foreach($taskFps in $FrameRates){$taskCases+=@{Name="Gameplay_$taskFps";FPS=$taskFps;Map='/Game/Maps/L_Mouth?Seed=41';Flag='-MCGamepadTest'}}
$taskResults=@()
foreach($taskCase in $taskCases){
    $taskLog=Join-Path $ReportDirectory ($taskCase.Name+'.log')
    $taskArgs=@()
    if(-not $BuildRoot){$taskArgs+=@("`"$taskRoot/MessControl.uproject`"",$taskCase.Map,'-game')}else{$taskArgs+=@($taskCase.Map)}
    $taskArgs+=@($taskCase.Flag,'-nosteam','-MCLegacyDays','-MCExpectedPlayers=1','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-unattended','-nosound','-nosplash',"`"-abslog=$taskLog`"","-ExecCmds=`"t.MaxFPS $($taskCase.FPS),t.IdleWhenNotForeground 0`"")
    if(-not $BuildRoot){$taskArgs+='-DisablePython'}
    Write-Output "START $($taskCase.Name)"
    $taskProcess=Start-Process -FilePath $taskExe -ArgumentList $taskArgs -WorkingDirectory (Split-Path $taskExe -Parent) -WindowStyle Hidden -PassThru
    try {
        if(-not $taskProcess.WaitForExit(60000)){throw "Gamepad validation timed out: $taskLog"}
        $taskText=Get-Content -LiteralPath $taskLog -Raw
        $taskMarkers=@($taskText -split "`n" | Where-Object {$_ -match 'MC_GAMEPAD_(PASS|FAIL)'})
        $taskErrors=@($taskText -split "`n" | Where-Object {$_ -match 'Fatal error:|: Error:|Failed to load package|Could not find a package file'})
        # Editor toolsets request GameFeatureData in -game mode, where its editor-only scan rule is inactive.
        # Preserve that unrelated startup diagnostic; the packaged target has no editor toolsets.
        $taskEditorDiagnostics=@()
        if(-not $BuildRoot){
            $taskEditorDiagnostics=@($taskErrors | Where-Object {$_ -match 'LogGameFeatures: Error: Asset manager settings do not include a rule for assets of type GameFeatureData,'})
            $taskErrors=@($taskErrors | Where-Object {$_ -notmatch 'LogGameFeatures: Error: Asset manager settings do not include a rule for assets of type GameFeatureData,'})
        }
        $taskPassed=$taskProcess.ExitCode -eq 0 -and $taskText -match 'MC_GAMEPAD_PASS' -and $taskText -notmatch 'MC_GAMEPAD_FAIL' -and $taskErrors.Count -eq 0
        $taskResults+=@{case=$taskCase.Name;fps=$taskCase.FPS;passed=$taskPassed;exitCode=$taskProcess.ExitCode;log=$taskLog;markers=$taskMarkers;errors=$taskErrors;editorOnlyStartupDiagnostics=$taskEditorDiagnostics;simulatedGamepadInput=$true;physicalControllerTested=$false;network=$false}
        $taskResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ReportDirectory 'Results.json') -Encoding utf8
        $taskMarkers | Write-Output
        if(-not $taskPassed){throw "Gamepad validation failed: $taskLog"}
    } finally {if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
}
