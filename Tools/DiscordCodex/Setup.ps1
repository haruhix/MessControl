param(
    [string]$GuildIds,
    [string]$UserIds,
    [string]$ChannelIds,
    [string]$OperatorIds,
    [switch]$SkipInstall
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1') -ErrorAction Stop
$taskProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$taskStateRoot = Join-Path $taskProjectRoot 'Saved\DiscordCodex'
$taskSecretRoot = Join-Path $env:LOCALAPPDATA 'MessControlDiscord'
$taskTokenPath = Join-Path $taskSecretRoot 'token.dpapi'
. (Join-Path $PSScriptRoot 'Find-Codex.ps1')
$taskCodexPath = Find-LocalCodex
$taskNodePath = (Get-Command node.exe -ErrorAction Stop).Source
if ([int]((& $taskNodePath --version).TrimStart('v').Split('.')[0]) -lt 22) { throw 'Install Node.js 22 or later.' }
& $taskCodexPath login status
if ($LASTEXITCODE -ne 0) {
    & $taskCodexPath login
    if ($LASTEXITCODE -ne 0) { throw 'Codex login failed.' }
}
Write-Host 'Enable Developer Mode in Discord: Settings > Advanced.'
Write-Host 'Right-click the server, users and optional channel > Copy ID.'
if (-not $GuildIds) { $GuildIds = Read-Host 'Discord server ID (comma-separated for several)' }
if (-not $UserIds) { $UserIds = Read-Host 'Your and colleague user IDs (comma-separated)' }
if (-not $PSBoundParameters.ContainsKey('ChannelIds')) { $ChannelIds = Read-Host 'Channel IDs (optional; empty = allowed users in any channel of this server)' }
if (-not $PSBoundParameters.ContainsKey('OperatorIds')) { $OperatorIds = Read-Host 'User IDs allowed to run git fetch/pull (optional; empty = no operators)' }
function Convert-DiscordIds([string]$Value, [bool]$Required) {
    $taskIds = @($Value -split '[,;\s]+' | Where-Object { $_ } | Select-Object -Unique)
    if ($Required -and $taskIds.Count -eq 0) { throw 'At least one ID is required.' }
    foreach ($taskId in $taskIds) { if ($taskId -notmatch '^\d{17,22}$') { throw "Invalid Discord ID: $taskId" } }
    return ,$taskIds
}
$taskAllowedGuilds = Convert-DiscordIds $GuildIds $true
$taskAllowedUsers = Convert-DiscordIds $UserIds $true
$taskAllowedChannels = Convert-DiscordIds $ChannelIds $false
$taskOperators = Convert-DiscordIds $OperatorIds $false
foreach ($taskId in $taskOperators) { if ($taskId -notin $taskAllowedUsers) { throw 'Operators must also be in allowed user IDs.' } }
New-Item -ItemType Directory -Path $taskStateRoot, $taskSecretRoot -Force | Out-Null
Write-Host 'Create a bot at https://discord.com/developers/applications (Bot > Reset Token).'
Write-Host 'Install it to your server with scopes: bot, applications.commands.'
Write-Host 'Permissions: View Channels, Send Messages, Send Messages in Threads, Attach Files, Read Message History.'
Write-Host 'Message Content Intent is not required: the bot responds to mentions and /mc commands.'
if ((Test-Path -LiteralPath $taskTokenPath) -and (Read-Host 'Reuse the saved encrypted Discord token? [Y/n]') -notmatch '^[nN]') {
    # Keep the existing encrypted token.
} else {
    $taskToken = Read-Host 'Paste Discord BOT token here (hidden; never paste it into chat)' -AsSecureString
    if ($taskToken.Length -lt 20) { throw 'The Discord bot token is empty or too short.' }
    $taskToken | ConvertFrom-SecureString | Set-Content -LiteralPath $taskTokenPath -Encoding ASCII
    $taskToken.Dispose()
}
$taskConfig = [ordered]@{
    projectRoot = $taskProjectRoot
    codexExecutable = $taskCodexPath
    allowedGuildIds = @($taskAllowedGuilds)
    allowedUserIds = @($taskAllowedUsers)
    allowedChannelIds = @($taskAllowedChannels)
    operatorUserIds = @($taskOperators)
    allowGitUpdates = $taskOperators.Count -gt 0
    timeZone = 'Asia/Yakutsk'
    turnTimeoutMs = 300000
    maxQueuedRequests = 6
}
$taskConfigPath = Join-Path $taskStateRoot 'config.json'
if (Test-Path -LiteralPath $taskConfigPath) {
    Copy-Item -LiteralPath $taskConfigPath -Destination (Join-Path $taskStateRoot ('config-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff') + '.json'))
}
$taskConfig | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskConfigPath -Encoding UTF8
if (-not $SkipInstall) {
    Push-Location $PSScriptRoot
    try {
        & npm.cmd ci --ignore-scripts --no-fund --no-audit
        if ($LASTEXITCODE -ne 0) { throw 'npm ci failed.' }
    } finally { Pop-Location }
}
Write-Host 'Setup complete. Run Start.ps1 (or double-click Start.cmd).'
