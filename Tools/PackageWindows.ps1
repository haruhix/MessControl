param(
    [string]$EngineRoot=$env:UE_ROOT,
    [string]$OutputDirectory,
    [switch]$SteamTest
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $EngineRoot) {
    $taskInstalls=(Get-Content -Raw 'C:/ProgramData/Epic/UnrealEngineLauncher/LauncherInstalled.dat' | ConvertFrom-Json).InstallationList
    $EngineRoot=($taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -First 1).InstallLocation
}
$taskUat=Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
if(-not(Test-Path -LiteralPath $taskUat)){throw 'UE 5.8 RunUAT.bat was not found.'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $taskRoot ('Saved/Builds/Win64_'+(Get-Date -Format 'yyyyMMdd_HHmmss'))}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$taskLog=Join-Path $OutputDirectory 'Package.log'
$taskEditorLog=Join-Path $OutputDirectory 'EditorBuild.log'
# Cook loads the editor module, so rebuild it before compiling the game target.
& "$PSScriptRoot/Build.ps1" -EngineRoot $EngineRoot *> $taskEditorLog
if($LASTEXITCODE -ne 0){throw "Editor build failed: $taskEditorLog"}
& $taskUat BuildCookRun "-project=$taskRoot/MessControl.uproject" -noP4 -platform=Win64 -clientconfig=Development -build -cook '-map=/Game/Maps/L_Mouth' -stage -pak -iostore -compressed -prereqs -nodebuginfo -unattended -utf8output -skipbuildeditor "-stagingdirectory=$OutputDirectory" *> $taskLog
if($LASTEXITCODE -ne 0){throw "Windows packaging failed ($LASTEXITCODE): $taskLog"}
$taskExe=Join-Path $OutputDirectory 'Windows/MessControl.exe'
if(-not(Test-Path -LiteralPath $taskExe)){throw "Packaged launcher missing: $taskExe"}
if($SteamTest) {
    $taskWindows=Split-Path -Parent $taskExe
    $taskGameBin=Join-Path $taskWindows 'MessControl/Binaries/Win64'
    '480' | Set-Content -LiteralPath (Join-Path $taskWindows 'steam_appid.txt') -Encoding ascii
    '480' | Set-Content -LiteralPath (Join-Path $taskGameBin 'steam_appid.txt') -Encoding ascii
    @'
@echo off
cd /d "%~dp0"
> "%~dp0steam_appid.txt" echo 480
> "%~dp0MessControl\Binaries\Win64\steam_appid.txt" echo 480
"%~dp0MessControl.exe" %*
'@ | Set-Content -LiteralPath (Join-Path $taskWindows 'Start_Steam.cmd') -Encoding ascii
    Copy-Item -LiteralPath (Join-Path $taskRoot 'Docs/SteamTesting.md') -Destination (Join-Path $taskWindows 'SteamTesting.md')
}
$taskCommit=& git -C $taskRoot rev-parse HEAD
$taskChanges=@(& git -C $taskRoot status --porcelain)
@{platform='Windows x64';configuration='Development';engine='5.8';steamTest=[bool]$SteamTest;baseCommit=$taskCommit;uncommittedChanges=$taskChanges;created=(Get-Date -Format o);executable=$taskExe} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'BuildInfo.json') -Encoding utf8
Write-Output "Windows build: $taskExe"
Write-Output "Package log: $taskLog"
