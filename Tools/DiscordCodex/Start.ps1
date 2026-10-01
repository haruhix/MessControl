param(
    [ValidateSet('run', 'doctor', 'smoke')][string]$Mode = 'run',
    [string]$ConfigPath,
    [switch]$Simple
)
$ErrorActionPreference = 'Stop'
# Codex can pass PowerShell 7's module path to Windows PowerShell 5.1.
# Load the security module matching this shell before reading DPAPI secrets.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1') -ErrorAction Stop
$taskProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $ConfigPath) { $ConfigPath = Join-Path $taskProjectRoot 'Saved\DiscordCodex\config.json' }
$taskNodePath = (Get-Command node.exe -ErrorAction Stop).Source
$taskPreviousToken = $env:DISCORD_BOT_TOKEN
try {
if ($Mode -eq 'run' -and -not (Test-Path -LiteralPath $ConfigPath)) {
    & (Join-Path $PSScriptRoot 'Setup.ps1')
}
if ($Mode -eq 'run') {
    $taskTokenPath = Join-Path $env:LOCALAPPDATA 'MessControlDiscord\token.dpapi'
    if (-not $env:DISCORD_BOT_TOKEN) {
        if (-not (Test-Path -LiteralPath $taskTokenPath)) { throw 'Run Setup.ps1 to save the Discord bot token.' }
        $taskSecure = (Get-Content -LiteralPath $taskTokenPath -Raw).Trim() | ConvertTo-SecureString
        $taskPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($taskSecure)
        try { $env:DISCORD_BOT_TOKEN = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($taskPointer) }
        finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($taskPointer); $taskSecure.Dispose() }
    }
}
$taskArgs = @((Join-Path $PSScriptRoot 'src\main.mjs'), $Mode, '--config', $ConfigPath)
. (Join-Path $PSScriptRoot 'Find-Codex.ps1')
$taskPreferredCodex = $null
if (Test-Path -LiteralPath $ConfigPath) {
    $taskSavedConfig = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
    $taskPreferredCodex = $taskSavedConfig.codexExecutable
}
$taskArgs += @('--codex-executable', (Find-LocalCodex $taskPreferredCodex))
if ($Simple) { $taskArgs += '--simple' }
    & $taskNodePath @taskArgs
    if ($LASTEXITCODE -ne 0) { throw "Discord Codex exited with code $LASTEXITCODE." }
} finally {
    if ($Mode -eq 'run') {
        if ($taskPreviousToken) { $env:DISCORD_BOT_TOKEN = $taskPreviousToken }
        else { Remove-Item Env:\DISCORD_BOT_TOKEN -ErrorAction SilentlyContinue }
    }
}
