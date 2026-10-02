param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [string]$ReportDirectory
)
$ErrorActionPreference='Stop'
$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
$taskExe=Join-Path $BuildRoot 'MessControl/Binaries/Win64/MessControl.exe'
$taskBin=Split-Path -Parent $taskExe
$taskReports=if($ReportDirectory){[IO.Path]::GetFullPath($ReportDirectory)}else{Join-Path (Split-Path $BuildRoot -Parent) 'SteamValidation'}
if(-not(Test-Path -LiteralPath $taskExe)){throw "Packaged executable missing: $taskExe"}
if(-not(Test-Path -LiteralPath (Join-Path $BuildRoot 'Start_Steam.cmd'))){throw 'Use a build packaged with -SteamTest.'}
if(-not(Get-Process -Name steam -ErrorAction SilentlyContinue)){throw 'Start Steam and sign in before testing.'}
New-Item -ItemType Directory -Path $taskReports -Force | Out-Null
$taskResults=@()
foreach($taskMode in @('Host','Find')) {
    # Unreal can remove this file at shutdown; restore it for each test.
    '480' | Set-Content -LiteralPath (Join-Path $taskBin 'steam_appid.txt') -Encoding ascii
    $taskLog=Join-Path $taskReports ($taskMode+'.log')
    $taskArgs=@("-MCSteam${taskMode}Test",'-RenderOffscreen','-windowed','-ForceRes','-ResX=960','-ResY=540','-unattended','-nosound','-nosplash','-nop4',"`"-abslog=$taskLog`"",'-ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0"')
    Write-Output "START Steam $taskMode (App ID 480, real backend)"
    $taskProcess=Start-Process -FilePath $taskExe -WorkingDirectory $taskBin -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
    $taskTimedOut=$false
    try {
        if(-not $taskProcess.WaitForExit(120000)) {
            $taskTimedOut=$true
            Stop-Process -Id $taskProcess.Id
            $taskProcess.WaitForExit()
        }
        $taskExit=$taskProcess.ExitCode
    } finally {
        if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}
    }
    $taskText=if(Test-Path -LiteralPath $taskLog){Get-Content -Raw -LiteralPath $taskLog}else{''}
    $taskMarks=@($taskText -split "`n" | Where-Object {$_ -match 'MC_STEAM_(READY|ROOM_CREATED|ROOMS_FOUND|TEST_PASS|TEST_FAIL)'})
    $taskOk=(-not $taskTimedOut -and $taskExit -eq 0 -and $taskText -match "MC_STEAM_TEST_PASS mode=$($taskMode.ToLower())\b" -and $taskText -notmatch 'MC_STEAM_TEST_FAIL|Fatal error:|: Error:')
    $taskResults+=@{mode=$taskMode;passed=$taskOk;exitCode=$taskExit;timedOut=$taskTimedOut;log=$taskLog;markers=$taskMarks;twoAccountConnectionTested=$false}
    $taskResults | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskReports 'Results.json') -Encoding utf8
    $taskMarks | ForEach-Object {Write-Output $_}
    if(-not $taskOk){throw "Steam $taskMode failed: $taskLog"}
}
Write-Output 'PASS: Steam lobby creation, SteamSockets listen driver and lobby search. Two-account joining requires two PCs.'
