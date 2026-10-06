param(
    [string]$EngineRoot=$env:UE_ROOT,
    [ValidateRange(1,4)][int]$Count=4,
    [ValidateSet('novice','regular','skilled')][string]$Skill='regular',
    [ValidateSet('coop','observe')][string]$Mode='observe',
    [int]$Seed=41,
    [ValidateRange(30,180)][int]$Seconds=180,
    [switch]$Render
)
$ErrorActionPreference='Stop'
$taskProjectRoot=Split-Path -Parent $PSScriptRoot
if(-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw -LiteralPath 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if(-not (Test-Path -LiteralPath $taskEditor)) { throw 'UE 5.8 editor not found. Pass -EngineRoot.' }
if($Mode -eq 'coop' -and $Count -eq 4) { throw 'Coop reserves one place for the human: use Count 1..3.' }
$taskProject=Join-Path $taskProjectRoot 'MessControl.uproject'
$taskLogs=Join-Path $taskProjectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $taskLogs -Force | Out-Null
$taskRunId=[guid]::NewGuid().ToString('N')
$taskLog=Join-Path $taskLogs "PlaytestBots_$taskRunId.log"
$taskArguments=@("`"$taskProject`"", "/Game/Maps/L_Mouth?Seed=$Seed", '-game','-MCBotsSmoke',"-MCBotsSmokeSeconds=$Seconds",
    '-unattended','-nosound','-nosplash','-nop4','-nosteam',"`"-abslog=$taskLog`"",
    '-ini:Engine:[/Script/PythonScriptPlugin.PythonScriptPluginSettings]:bRemoteExecution=False',
    "-ExecCmds=`"MC.Bots $Count $Skill $Mode $Seed,t.MaxFPS 30,t.IdleWhenNotForeground 0`"")
if($Render) { $taskArguments+=@('-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720') }
else { $taskArguments+='-nullrhi' }
$taskProcess=Start-Process -FilePath $taskEditor -ArgumentList $taskArguments -WindowStyle Hidden -PassThru
try {
    if(-not $taskProcess.WaitForExit(($Seconds+90)*1000)) { throw "Bot smoke timed out. Log: $taskLog" }
    $taskText=Get-Content -Raw -LiteralPath $taskLog
    if($taskProcess.ExitCode -ne 0 -or $taskText -notmatch 'MC_BOTS_SMOKE_PASS' -or $taskText -match 'MC_BOTS_SMOKE_FAIL') {
        throw "Bot runtime validation failed. Log: $taskLog"
    }
    Write-Output "PASS: real movement, gameplay contact and a fully cleaned tooth on L_Mouth. Log: $taskLog"
    Write-Output "CSV: $taskProjectRoot\Saved\Playtests"
} finally {
    if(-not $taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id }
}
