param([string]$DataDirectory = (Join-Path $PSScriptRoot 'data'))
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Launcher.Common.ps1')
$taskRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$taskData = Resolve-DeskPath $DataDirectory $taskRoot
$recordPath = Join-Path $taskData 'server.json'
if (-not (Test-Path -LiteralPath $recordPath -PathType Leaf)) { Write-Host 'No launcher-owned Audio Desk server record was found.';exit 0 }
$record = Get-Content -LiteralPath $recordPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not (Test-DeskSamePath $record.data_dir $taskData) -or -not (Test-DeskSamePath $record.app_dir $taskRoot)) { throw 'The server record belongs to another application folder.' }
$expectedPrefix = (Join-Path $taskRoot '.runtime').TrimEnd('\') + '\'
$expectedExe = [IO.Path]::GetFullPath([string]$record.app_path)
if (-not $expectedExe.StartsWith($expectedPrefix,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($expectedExe) -ne 'AudioDesk.exe') { throw 'Unexpected executable path in the server record.' }
$running = Get-CimInstance Win32_Process -Filter "ProcessId=$([int]$record.pid)" -ErrorAction SilentlyContinue
if ($running) {
    if (-not (Test-DeskSamePath $running.ExecutablePath $expectedExe)) { throw 'The process ID now belongs to a different application; it will not be stopped.' }
    if (-not (Get-DeskHealth ([int]$record.port) $taskData $taskRoot)) { throw 'The process does not confirm this Audio Desk data directory; it will not be stopped.' }
    if ($running.CommandLine -notlike '*--data-dir*' -or $running.CommandLine.IndexOf($taskData,[StringComparison]::OrdinalIgnoreCase) -lt 0) { throw 'The process command line does not match this launcher; it will not be stopped.' }
    Stop-Process -Id ([int]$record.pid) -ErrorAction Stop
}
Remove-Item -LiteralPath $recordPath -Force
$pidPath = Join-Path $taskData 'server.pid'
if (Test-Path -LiteralPath $pidPath) { Remove-Item -LiteralPath $pidPath -Force }
Write-Host 'Audio Desk stopped. Saved profiles and audio files were preserved.'
