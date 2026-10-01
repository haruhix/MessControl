param(
    [string]$PythonPath,
    [string]$CodebasePath,
    [switch]$SkipDownload
)
$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $PythonPath) { $PythonPath = (Get-Command python.exe -ErrorAction Stop).Source }
if (-not $CodebasePath) { $CodebasePath = Join-Path $env:LOCALAPPDATA 'Programs\codebase-memory-mcp-0.11.0\codebase-memory-mcp.exe' }
if (-not (Test-Path -LiteralPath $CodebasePath)) {
    if ($SkipDownload) { throw "codebase-memory 0.11.0 not found: $CodebasePath" }
    $taskDownloads = Join-Path $taskProjectRoot 'Saved\ProjectIndex\Downloads'
    New-Item -ItemType Directory -Path $taskDownloads -Force | Out-Null
    $taskZip = Join-Path $taskDownloads 'codebase-memory-mcp-windows-amd64.zip'
    & curl.exe -fsSL --connect-timeout 10 --max-time 300 'https://github.com/DeusData/codebase-memory-mcp/releases/download/v0.11.0/codebase-memory-mcp-windows-amd64.zip' -o $taskZip
    if ($LASTEXITCODE -ne 0) { throw 'Download failed' }
    # Pinned official release checksum; no downloaded installer or integration hook is executed.
    $taskExpectedHash = '6eb6beaf261b19e419766e78baf93cbc3cf1c6338cff8fb7c0234859f96d1685'
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $taskZip).Hash -ne $taskExpectedHash) { throw 'Release checksum mismatch' }
    Expand-Archive -LiteralPath $taskZip -DestinationPath (Split-Path -Parent $CodebasePath)
}
$taskCodex = (Get-Command codex.exe -ErrorAction Stop).Source
$taskCodexDir = if ($env:CODEX_HOME) { $env:CODEX_HOME } else { Join-Path $env:USERPROFILE '.codex' }
$taskConfig = Join-Path $taskCodexDir 'config.toml'
if (Test-Path -LiteralPath $taskConfig) {
    # Re-running setup may update our servers, but must not replace an unrelated server using the same name.
    $taskReadServers = @'
import json, pathlib, sys, tomllib
config = tomllib.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8'))
print(json.dumps({name: row.get('args', []) for name, row in config.get('mcp_servers', {}).items()
                 if name in ('codebase_memory', 'messcontrol_assets')}))
'@
    $taskExisting = & $PythonPath -c $taskReadServers $taskConfig | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the existing MCP configuration' }
    foreach ($taskName in @('codebase_memory', 'messcontrol_assets')) {
        $taskEntry = $taskExisting.PSObject.Properties[$taskName]
        if ($null -eq $taskEntry) { continue }
        $taskArgs = @($taskEntry.Value)
        $taskWrapper = if ($taskName -eq 'codebase_memory') { 'codebase_server.py' } else { 'server.py' }
        $taskExpectedWrapper = Join-Path $PSScriptRoot $taskWrapper
        $taskProjectFlag = [Array]::IndexOf($taskArgs, '--project')
        if ($taskArgs.Count -lt 1 -or [IO.Path]::GetFullPath($taskArgs[0]) -ne $taskExpectedWrapper -or
            $taskProjectFlag -lt 0 -or $taskProjectFlag + 1 -ge $taskArgs.Count -or
            [IO.Path]::GetFullPath($taskArgs[$taskProjectFlag + 1]) -ne $taskProjectRoot) {
            throw "MCP '$taskName' already belongs to another installation. Choose a distinct server name before setup."
        }
    }
    $taskBackupDir = Join-Path $taskCodexDir 'backups\messcontrol-project-index'
    New-Item -ItemType Directory -Path $taskBackupDir -Force | Out-Null
    Copy-Item -LiteralPath $taskConfig -Destination (Join-Path $taskBackupDir ('config-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff') + '.toml'))
}
& $taskCodex mcp add codebase_memory -- $PythonPath (Join-Path $PSScriptRoot 'codebase_server.py') --project $taskProjectRoot --binary $CodebasePath
if ($LASTEXITCODE -ne 0) { throw 'Adding codebase MCP failed' }
& $taskCodex mcp add messcontrol_assets -- $PythonPath (Join-Path $PSScriptRoot 'server.py') --project $taskProjectRoot
if ($LASTEXITCODE -ne 0) { throw 'Adding asset MCP failed' }
foreach ($taskSetting in @(@('auto_index', 'true'), @('auto_watch', 'true'), @('watcher_enabled', 'true'), @('ui_enabled', 'false'))) {
    & $CodebasePath config set $taskSetting[0] $taskSetting[1]
    if ($LASTEXITCODE -ne 0) { throw "Setting codebase-memory $($taskSetting[0]) failed" }
}
Write-Output 'Configured codebase_memory and messcontrol_assets. Reopen the project chat or restart Codex to load the new tools.'
