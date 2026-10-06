param([string]$EngineRoot=$env:UE_ROOT)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw -LiteralPath 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProject=Join-Path $taskRoot 'MessControl.uproject'
$taskReport=Join-Path $taskRoot 'Saved\PermanentToolsTests'
$taskLog=Join-Path $taskRoot 'Saved\Logs\PermanentToolsTests.log'
New-Item -ItemType Directory -Force -Path $taskReport,(Split-Path -Parent $taskLog) | Out-Null
$taskResult=Join-Path $taskReport 'index.json'
if(Test-Path -LiteralPath $taskResult) {Remove-Item -LiteralPath $taskResult}
$taskArgs=@("`"$taskProject`"",'-unattended','-nop4','-nosplash','-nosteam','-nullrhi',
    '-ExecCmds="Automation SetFilter Engine; RunTests MessControl.Tutorial+MessControl.Inventory+MessControl.DayOne.PermanentBrushAndLegacyDisposal+MessControl.Liquid.AuthoritativeBrushContact; Quit"',
    '-TestExit="Automation Test Queue Empty"',"`"-ReportExportPath=$taskReport`"","`"-abslog=$taskLog`"")
$taskProcess=$null
try {
    $taskProcess=Start-Process -FilePath $taskEditor -WindowStyle Hidden -PassThru -ArgumentList $taskArgs
    if(-not $taskProcess.WaitForExit(180000)) {throw "Permanent-tool tests timed out. See $taskLog"}
    if($taskProcess.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $taskResult)) {throw "Permanent-tool tests failed. See $taskLog"}
    $taskResults=Get-Content -Raw -LiteralPath $taskResult | ConvertFrom-Json
    if($taskResults.failed -ne 0 -or ($taskResults.succeeded+$taskResults.succeededWithWarnings) -lt 13) {
        throw "Not all permanent-tool and tutorial checks passed. See $taskLog"
    }
    Write-Output "PASS: $($taskResults.succeeded+$taskResults.succeededWithWarnings) permanent-tool, tutorial, cleaning and day-flow checks."
} finally {
    if($taskProcess -and -not $taskProcess.HasExited) {Stop-Process -Id $taskProcess.Id}
}
