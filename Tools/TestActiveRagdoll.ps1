param([string]$EngineRoot='E:\UE\UE_5.8',[switch]$Solo,[switch]$Review,[int]$PacketLagMs=75,[int]$PacketLoss=2)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskExe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProject=Join-Path $taskRoot 'MessControl.uproject'
$taskFolder=Join-Path $taskRoot 'Saved\ActiveRagdollValidation'
New-Item -ItemType Directory -Path $taskFolder -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $taskRoot 'Artifacts\ActiveRagdoll') -Force | Out-Null
if($Review) {
    $taskCapturePath=[IO.Path]::GetFullPath((Join-Path $taskRoot 'Saved\ApprovalFrames\ActiveRagdoll'))
    $taskExpectedPrefix=[IO.Path]::GetFullPath((Join-Path $taskRoot 'Saved\ApprovalFrames'))+[IO.Path]::DirectorySeparatorChar
    if(-not $taskCapturePath.StartsWith($taskExpectedPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Capture directory is outside this project.'}
    if(Test-Path -LiteralPath $taskCapturePath) {
        Get-ChildItem -LiteralPath $taskCapturePath -File | Where-Object {$_.Name -match '^\d{6}\.png$' -or $_.Name -eq 'times.csv'} | ForEach-Object {Remove-Item -LiteralPath $_.FullName}
    }
}
$taskCount=if($Solo){1}else{4}
$taskProcesses=@()
try {
    for($taskIndex=0;$taskIndex -lt $taskCount;$taskIndex++) {
        $taskMap=if($Solo){'/Game/Maps/L_Mouth?Seed=41'}elseif($taskIndex -eq 0){'/Game/Maps/L_Mouth?listen?Seed=41'}else{'127.0.0.1:7777'}
        $taskLog=Join-Path $taskFolder "Run$taskIndex.log"
        if(Test-Path -LiteralPath $taskLog){Remove-Item -LiteralPath $taskLog}
        $taskArgs=@("`"$taskProject`"",$taskMap,'-game','-MCLegacyDays','-MCActiveRagdollTest',"-MCExpectedPlayers=$taskCount",'-nullrhi','-unattended','-nosound','-nosplash','-nop4','"-ExecCmds=t.MaxFPS 60,t.IdleWhenNotForeground 0"',"`"-abslog=$taskLog`"")
        if(-not $Solo){$taskArgs+=@("-PktLag=$PacketLagMs","-PktLoss=$PacketLoss")}
        if($Review -and $taskIndex -eq 0) {
            $taskArgs=@($taskArgs | Where-Object {$_ -ne '-nullrhi' -and $_ -notlike '*ExecCmds=*'})
            $taskArgs+=@('-MCActiveRagdollCapture','-MCVideo=ActiveRagdoll','-RenderOffscreen','-windowed','-ForceRes','-ResX=1280','-ResY=720','-NoScreenMessages','"-ExecCmds=t.MaxFPS 60,t.IdleWhenNotForeground 0,sg.GlobalIlluminationQuality 2,sg.ReflectionQuality 2,sg.ShadowQuality 2,r.ScreenPercentage 100,Trace.Disable Screenshot"')
            if($Solo){$taskArgs+=@('-UseFixedTimeStep','-FPS=30')}
        }
        $taskProcesses+=Start-Process -FilePath $taskExe -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
        if($taskIndex -eq 0 -and -not $Solo) {
            $taskDeadline=(Get-Date).AddSeconds(60)
            do {
                Start-Sleep -Milliseconds 500
                if($taskProcesses[0].HasExited){throw 'Host exited before listening.'}
                $taskListening=(Test-Path -LiteralPath $taskLog) -and (Select-String -LiteralPath $taskLog -Pattern 'listening on port 7777' -Quiet)
            } until($taskListening -or (Get-Date) -gt $taskDeadline)
            if(-not $taskListening){throw 'Host did not start listening.'}
        }
    }
    foreach($taskProcess in $taskProcesses){if(-not $taskProcess.WaitForExit(240000)){throw 'Active ragdoll validation timed out.'}}
    $taskResults=for($taskIndex=0;$taskIndex -lt $taskCount;$taskIndex++) {
        $taskLog=Join-Path $taskFolder "Run$taskIndex.log"
        $taskMatch=Select-String -LiteralPath $taskLog -Pattern 'MC_ACTIVE_RAGDOLL_(PASS|FAIL)' | Select-Object -Last 1
        if(-not $taskMatch -or $taskMatch.Line -notmatch 'MC_ACTIVE_RAGDOLL_PASS'){throw "Active ragdoll validation failed: $taskLog"}
        $taskMatch.Line
    }
    $taskResults | Set-Content -LiteralPath (Join-Path $taskFolder 'Results.txt') -Encoding utf8
    $taskResults
} finally {
    foreach($taskProcess in $taskProcesses){if(-not $taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
}
