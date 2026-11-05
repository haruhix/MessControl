param(
    [string]$EngineRoot=$env:UE_ROOT,
    [string]$OutputDirectory,
    [switch]$SteamTest,
    [switch]$BakeFoodCollision,
    [switch]$UseZenStore,
    [ValidateRange(1,256)][int]$ShaderWorkers=12,
    [string[]]$UbtArguments=@()
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
if([IO.Path]::GetPathRoot($OutputDirectory).TrimEnd('\','/') -ine 'E:') {throw 'Windows packaging output must be on drive E:.'}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$taskLog=Join-Path $OutputDirectory 'Package.log'
$taskEditorLog=Join-Path $OutputDirectory 'EditorBuild.log'
$taskTemporary=Join-Path $OutputDirectory 'Temp'
$taskUatLogs=Join-Path $OutputDirectory 'UATLogs'
$taskCookOutput=Join-Path $OutputDirectory 'Cooked/Windows'
[string[]]$taskEffectiveUbtArguments=@($UbtArguments)
if(-not($taskEffectiveUbtArguments | Where-Object { $_ -match '^-(?:No)?UBA(?:=|$)' })) {
    $taskEffectiveUbtArguments+='-UBA'
}
if(-not($taskEffectiveUbtArguments | Where-Object { $_ -match '^-MaxParallelActions(?:=|$)' })) {
    $taskEffectiveUbtArguments+='-MaxParallelActions=4'
}
if(-not($taskEffectiveUbtArguments | Where-Object { $_ -match '^-UBARootDir(?:=|$)' })) {
    $taskEffectiveUbtArguments+="-UBARootDir=$(Join-Path $OutputDirectory 'UBA')"
}
$taskLogicalCores=[Environment]::ProcessorCount
$taskShaderWorkers=[Math]::Min($ShaderWorkers,[Math]::Max(1,$taskLogicalCores-1))
$taskUnusedShaderThreads=$taskLogicalCores-$taskShaderWorkers
$taskCookStorage=if($UseZenStore){'ZenStore'}else{'LooseFiles'}
[string[]]$taskCookerArguments=@(
    '-ddc=InstalledNoZenLocalFallback'
    "-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=$taskUnusedShaderThreads"
)
if(-not $UseZenStore){$taskCookerArguments=@('-SkipZenStore')+$taskCookerArguments}
$taskProvidedDdc=[Environment]::GetEnvironmentVariable('UE-LocalDataCachePath','Process')
$taskDdc=if($taskProvidedDdc -match '^E:[\\/]') {
    [IO.Path]::GetFullPath($taskProvidedDdc)
} else {
    Join-Path $taskRoot 'Saved/DerivedDataCache'
}
New-Item -ItemType Directory -Path $taskTemporary,$taskUatLogs,$taskDdc,$taskCookOutput -Force | Out-Null
$taskEnvironment=@{
    TEMP=$taskTemporary;TMP=$taskTemporary;TMPDIR=$taskTemporary
    uebp_LogFolder=$taskUatLogs;uebp_FinalLogFolder=$taskUatLogs
    uebp_EngineSavedFolder=(Join-Path $OutputDirectory 'AutomationSaved')
    'UE-LocalDataCachePath'=$taskDdc
}
$taskPreviousEnvironment=@{}
foreach($taskName in $taskEnvironment.Keys) {
    $taskPreviousEnvironment[$taskName]=[Environment]::GetEnvironmentVariable($taskName,'Process')
}
try {
    foreach($taskName in $taskEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($taskName,$taskEnvironment[$taskName],'Process')
    }
    # Cook loads the editor module, so rebuild it before compiling the game target.
    $taskEditorArguments=@("-Log=$OutputDirectory/UBTEditor.log")+$taskEffectiveUbtArguments
    & "$PSScriptRoot/Build.ps1" -EngineRoot $EngineRoot -AdditionalArguments $taskEditorArguments *> $taskEditorLog
    if($LASTEXITCODE -ne 0){throw "Editor build failed: $taskEditorLog"}
    # Packaging validates saved collision profiles. Authoring is an explicit opt-in.
    if($BakeFoodCollision) {
        & "$PSScriptRoot/OptimizeFoodCollision.ps1" -EngineRoot $EngineRoot *> (Join-Path $OutputDirectory 'FoodCollisionBake.log')
    } else {
        & "$PSScriptRoot/OptimizeFoodCollision.ps1" -EngineRoot $EngineRoot -AuditOnly *> (Join-Path $OutputDirectory 'FoodCollisionAudit.log')
    }
    if($LASTEXITCODE -ne 0){throw "Food collision validation failed ($LASTEXITCODE). Inspect the FoodCollisionAudit/Bake log in $OutputDirectory."}
    [string[]]$taskUatExtra=@()
    if($taskEffectiveUbtArguments.Count){$taskUatExtra=@("-UbtArgs=$($taskEffectiveUbtArguments -join ' ')")}
    if($UseZenStore){$taskUatExtra+='-zenstore'}
    # UAT staging appends the platform unless CookOutputDir already ends in Windows.
    # Keep loose cook independent of the optional local Zen service; IoStore still
    # creates the ordinary distributable containers during staging.
    $taskUatExtra+="-CookOutputDir=$taskCookOutput"
    $taskUatExtra+="-AdditionalCookerOptions=$($taskCookerArguments -join ' ')"
    # No command-line -map override: use the project's complete MapsToCook list.
    & $taskUat BuildCookRun "-project=$taskRoot/MessControl.uproject" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -iostore -compressed -prereqs -nodebuginfo -unattended -utf8output -skipbuildeditor "-stagingdirectory=$OutputDirectory" @taskUatExtra *> $taskLog
    if($LASTEXITCODE -ne 0){throw "Windows packaging failed ($LASTEXITCODE): $taskLog"}
} finally {
    foreach($taskName in $taskEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($taskName,$taskPreviousEnvironment[$taskName],'Process')
    }
}
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
$taskChanges=@(& git -C $taskRoot status --porcelain --untracked-files=no)
@{platform='Windows x64';configuration='Development';engine='5.8';steamTest=[bool]$SteamTest;foodCollisionBake=[bool]$BakeFoodCollision;mapSelection='Project MapsToCook';ubtArguments=$taskEffectiveUbtArguments;requestedUbtArguments=$UbtArguments;localDerivedDataCache=$taskDdc;cookStorage=$taskCookStorage;cookOutputDirectory=$taskCookOutput;ioStore=$true;shaderWorkersRequested=$ShaderWorkers;shaderWorkers=$taskShaderWorkers;logicalCores=$taskLogicalCores;unusedShaderThreads=$taskUnusedShaderThreads;additionalCookerOptions=$taskCookerArguments;baseCommit=$taskCommit;uncommittedChanges=$taskChanges;trackedChangesOnly=$true;created=(Get-Date -Format o);executable=$taskExe} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'BuildInfo.json') -Encoding utf8
Write-Output "Windows build: $taskExe"
Write-Output "Package log: $taskLog"
