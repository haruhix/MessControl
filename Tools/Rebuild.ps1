param(
    [Parameter(Position = 0)][string]$EngineRoot,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$taskProjectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskProjectFile = Join-Path $taskProjectRoot 'MessControl.uproject'
$taskBuildScript = Join-Path $PSScriptRoot 'Build.ps1'

try {
    if (-not (Test-Path -LiteralPath $taskProjectFile -PathType Leaf)) {
        throw 'MessControl.uproject was not found next to RebuildProject.bat.'
    }
    if (-not (Test-Path -LiteralPath $taskBuildScript -PathType Leaf)) {
        throw 'Tools\Build.ps1 was not found.'
    }
    if (Get-Process -Name UnrealEditor, UnrealEditor-Cmd, LiveCodingConsole -ErrorAction SilentlyContinue) {
        throw 'Save your work and close Unreal Editor, commandlets and Live Coding before rebuilding.'
    }

    if (-not $EngineRoot) { $EngineRoot = $env:UE_ROOT }
    if (-not $EngineRoot) { $EngineRoot = $env:UE_ENGINE_ROOT }
    if (-not $EngineRoot) {
        $taskCandidates = @()
        $taskManifest = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
        if (Test-Path -LiteralPath $taskManifest) {
            try {
                $taskInstalls = (Get-Content -Raw -LiteralPath $taskManifest | ConvertFrom-Json).InstallationList
                $taskCandidates += $taskInstalls | Where-Object AppName -eq 'UE_5.8' | Select-Object -ExpandProperty InstallLocation
            } catch {
                Write-Warning "Could not read Epic Launcher installations: $($_.Exception.Message)"
            }
        }
        foreach ($taskKey in @(
            'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8',
            'HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\5.8'
        )) {
            $taskInstall = Get-ItemProperty -LiteralPath $taskKey -Name InstalledDirectory -ErrorAction SilentlyContinue
            if ($taskInstall) { $taskCandidates += $taskInstall.InstalledDirectory }
        }
        $taskCandidates += @('E:\UE\UE_5.8', 'C:\Program Files\Epic Games\UE_5.8', 'D:\Epic Games\UE_5.8')
        $EngineRoot = $taskCandidates | Where-Object {
            $_ -and (Test-Path -LiteralPath (Join-Path $_ 'Engine\Build\BatchFiles\Build.bat') -PathType Leaf)
        } | Select-Object -First 1
    }
    if (-not $EngineRoot -or -not (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') -PathType Leaf)) {
        throw 'UE 5.8 was not found. Pass the engine directory as the first argument, or set UE_ROOT / UE_ENGINE_ROOT.'
    }
    $EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).ProviderPath

    Write-Host "Project: $taskProjectFile"
    Write-Host "Engine:  $EngineRoot"
    Write-Host 'Content, Config, Source, autosaves and local settings in Saved are preserved.'
    if ($DryRun) { Write-Host '[DRY RUN] No files will be removed and no build will be started.' }

    $taskGeneratedPaths = @(
        'Binaries',
        'Intermediate',
        'DerivedDataCache',
        '.vs',
        'Saved\Cooked',
        'Saved\Crashes',
        'Saved\HotReload',
        'Saved\Logs',
        'Saved\ShaderDebugInfo',
        'Saved\StagedBuilds',
        'Saved\Temp'
    )
    $taskProjectPrefix = $taskProjectRoot.TrimEnd('\') + '\'

    # Validate every absolute target before deleting anything. Refuse junctions/symlinks
    # in the cleanup paths so generated directories cannot redirect outside the project.
    $taskCleanTargets = foreach ($taskRelativePath in $taskGeneratedPaths) {
        $taskTarget = [IO.Path]::GetFullPath((Join-Path $taskProjectRoot $taskRelativePath))
        if (-not $taskTarget.StartsWith($taskProjectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Cleanup target is outside the project: $taskTarget"
        }
        $taskProbe = $taskTarget
        while ($taskProbe -ne $taskProjectRoot) {
            $taskItem = Get-Item -LiteralPath $taskProbe -Force -ErrorAction SilentlyContinue
            if ($taskItem -and ($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Cleanup path contains a junction or symbolic link: $taskProbe"
            }
            $taskProbe = Split-Path -Parent $taskProbe
        }
        if (Test-Path -LiteralPath $taskTarget) {
            $taskLink = Get-ChildItem -LiteralPath $taskTarget -Recurse -Force -ErrorAction Stop |
                Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
            if ($taskLink) { throw "Cleanup directory contains a junction or symbolic link: $($taskLink.FullName)" }
        }
        $taskTarget
    }

    foreach ($taskTarget in $taskCleanTargets) {
        if (Test-Path -LiteralPath $taskTarget) {
            if ($DryRun) {
                Write-Host "Would remove: $taskTarget"
            } else {
                Write-Host "Removing: $taskTarget"
                Remove-Item -LiteralPath $taskTarget -Recurse -Force -ErrorAction Stop
            }
        }
    }

    Write-Host '[BUILD] MessControlEditor Win64 Development'
    if (-not $DryRun) {
        & $taskBuildScript -EngineRoot $EngineRoot
        Write-Host '[OK] Clean rebuild completed. You can open MessControl.uproject now.'
    }
    exit 0
} catch {
    Write-Host "[ERROR] $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
