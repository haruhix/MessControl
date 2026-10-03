param([string]$EngineRoot='E:\UE\UE_5.8',[switch]$EncodeOnly,[string]$VideoName='')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $VideoName) {$VideoName='DashSuction_'+(Get-Date -Format 'yyyyMMdd_HHmmss')}
if($VideoName -notmatch '^[A-Za-z0-9_-]+$') {throw 'Invalid recording name.'}
if(-not $EncodeOnly) {
    $taskFrames=Join-Path $taskRoot "Saved\ApprovalFrames\$VideoName"
    if(Test-Path -LiteralPath $taskFrames) {throw "Recording already exists: $taskFrames"}
    & "$PSScriptRoot\TestDashSuction.ps1" -EngineRoot $EngineRoot -VideoName $VideoName -PacketLagMs 75 -PacketLoss 2
}
& python "$PSScriptRoot\encode_dash_suction.py" $VideoName
if($LASTEXITCODE -ne 0) {throw 'Dash/suction video encoding failed.'}
