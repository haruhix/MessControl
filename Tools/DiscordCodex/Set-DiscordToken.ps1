$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1') -ErrorAction Stop
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$taskSecretRoot = Join-Path $env:LOCALAPPDATA 'MessControlDiscord'
$taskTokenPath = Join-Path $taskSecretRoot 'token.dpapi'
$taskStatusPath = Join-Path $taskSecretRoot 'token-input-status.json'
New-Item -ItemType Directory -Path $taskSecretRoot -Force | Out-Null
function Write-TokenInputStatus([hashtable]$Value) {
    $Value | ConvertTo-Json | Set-Content -LiteralPath $taskStatusPath -Encoding UTF8
}
Write-TokenInputStatus @{ status = 'awaiting_input' }
$taskSaved = $false
$taskForm = New-Object System.Windows.Forms.Form
$taskForm.Text = 'Discord Bot Token - MessControl'
$taskForm.ClientSize = New-Object System.Drawing.Size(520, 180)
$taskForm.StartPosition = 'CenterScreen'
$taskForm.FormBorderStyle = 'FixedDialog'
$taskForm.MaximizeBox = $false
$taskForm.MinimizeBox = $false
$taskForm.TopMost = $true
$taskLabel = New-Object System.Windows.Forms.Label
$taskLabel.Text = "Paste the Discord BOT token (Ctrl+V), then click Save.`r`nThe input is hidden. The token is stored locally with Windows encryption."
$taskLabel.Location = New-Object System.Drawing.Point(16, 16)
$taskLabel.Size = New-Object System.Drawing.Size(488, 42)
$taskInput = New-Object System.Windows.Forms.TextBox
$taskInput.UseSystemPasswordChar = $true
$taskInput.Location = New-Object System.Drawing.Point(16, 68)
$taskInput.Size = New-Object System.Drawing.Size(488, 28)
$taskSave = New-Object System.Windows.Forms.Button
$taskSave.Text = 'Save'
$taskSave.Location = New-Object System.Drawing.Point(304, 118)
$taskSave.Size = New-Object System.Drawing.Size(96, 34)
$taskCancel = New-Object System.Windows.Forms.Button
$taskCancel.Text = 'Cancel'
$taskCancel.Location = New-Object System.Drawing.Point(408, 118)
$taskCancel.Size = New-Object System.Drawing.Size(96, 34)
$taskCancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$taskForm.Controls.AddRange(@($taskLabel, $taskInput, $taskSave, $taskCancel))
$taskForm.AcceptButton = $taskSave
$taskForm.CancelButton = $taskCancel
$taskForm.Add_Shown({ $taskForm.Activate(); $taskInput.Focus() })
$taskSave.Add_Click({
    $taskPlainToken = $taskInput.Text.Trim()
    if ($taskPlainToken.Length -lt 20 -or $taskPlainToken -match '\s') {
        [System.Windows.Forms.MessageBox]::Show($taskForm, 'Paste the full Bot Token from the Discord Bot page.', 'Token required') | Out-Null
        return
    }
    $taskSave.Enabled = $false
    $taskForm.UseWaitCursor = $true
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        # Only validate the bot's own identity. Never print headers or the token.
        $taskProfile = Invoke-RestMethod -Uri 'https://discord.com/api/v10/users/@me' -Method Get -Headers @{ Authorization = 'Bot ' + $taskPlainToken } -TimeoutSec 15
        if (-not $taskProfile.bot) { throw 'Not a bot token.' }
        $taskSecureToken = ConvertTo-SecureString $taskPlainToken -AsPlainText -Force
        try {
            $taskSecureToken | ConvertFrom-SecureString | Set-Content -LiteralPath $taskTokenPath -Encoding ASCII
        } finally { $taskSecureToken.Dispose() }
        Write-TokenInputStatus @{ status = 'saved'; botId = [string]$taskProfile.id; botName = [string]$taskProfile.username }
        $script:taskSaved = $true
        $taskInput.Clear()
        $taskForm.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $taskForm.Close()
    } catch {
        [System.Windows.Forms.MessageBox]::Show($taskForm, 'Could not validate or save the token. Check the token and your connection, then try again.', 'Token not saved') | Out-Null
    } finally {
        $taskPlainToken = $null
        $taskSave.Enabled = $true
        $taskForm.UseWaitCursor = $false
    }
})
try { $taskForm.ShowDialog() | Out-Null }
finally {
    $taskInput.Clear()
    $taskForm.Dispose()
    if (-not $taskSaved) { Write-TokenInputStatus @{ status = 'cancelled' } }
}
