param(
    [ValidateRange(0,65535)][int]$Port = 0,
    [string]$DataDirectory = (Join-Path $PSScriptRoot 'data'),
    [switch]$NoBrowser,
    [switch]$NoDownload
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Launcher.Common.ps1')
$taskRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$taskData = Resolve-DeskPath $DataDirectory $taskRoot
New-Item -ItemType Directory -Path $taskData -Force | Out-Null
$taskTemp = Join-Path $taskData 'temp'
New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$env:TEMP = $taskTemp
$env:TMP = $taskTemp
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$ports = if ($Port) { @($Port) } else { @(8765..8785) }
$freePort = 0
foreach ($candidate in $ports) {
    if (Test-DeskPortFree $candidate) { if (-not $freePort) { $freePort = $candidate }; continue }
    $health = Get-DeskHealth $candidate $taskData $taskRoot
    if ($health) {
        $address = "http://127.0.0.1:$candidate/"
        Write-Host "Audio Desk is already running: $address"
        if (-not $NoBrowser) { Start-Process $address }
        exit 0
    }
}
if (-not $freePort) { throw 'No free Audio Desk port is available. Stop the existing application or select -Port.' }
$runtime = Install-DeskBundle $taskRoot 'runtime-manifest.json' '.runtime' -NoDownload:$NoDownload
$libraryManifest = Get-DeskManifest $taskRoot 'library-manifest.json'
$libraryRoot = if ($libraryManifest.extracted_directory) { [string]$libraryManifest.extracted_directory } else { 'library' }
if ($libraryRoot -ne 'library') { throw 'The bundled library must use the library/ folder.' }
$null = Install-DeskBundle $taskRoot 'library-manifest.json' 'library' -ArchiveRoot $libraryRoot -NoDownload:$NoDownload
$manifest = Get-DeskManifest $taskRoot 'runtime-manifest.json'
$executable = [IO.Path]::GetFullPath((Join-Path $runtime $manifest.app_relative_path))
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'AudioDesk.exe is missing. Remove the managed .runtime folder and start again.' }
$taskArguments = @('--port', "$freePort", '--app-dir', ('"' + $taskRoot + '"'), '--data-dir', ('"' + $taskData + '"'))
$process = Start-Process -FilePath $executable -ArgumentList $taskArguments -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $taskData 'server.stdout.log') -RedirectStandardError (Join-Path $taskData 'server.stderr.log')
$record = [ordered]@{pid=$process.Id;port=$freePort;data_dir=$taskData;app_path=$executable;app_dir=$taskRoot;started_at=[DateTime]::UtcNow.ToString('o')}
$record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskData 'server.json') -Encoding UTF8
Set-Content -LiteralPath (Join-Path $taskData 'server.pid') -Value $process.Id -Encoding ASCII
$ready = $false
for ($attempt=0; $attempt -lt 180; $attempt++) {
    $process.Refresh()
    if ($process.HasExited) { throw "Audio Desk exited. Read $taskData\server.stderr.log" }
    if (Get-DeskHealth $freePort $taskData $taskRoot) { $ready=$true;break }
    Start-Sleep -Milliseconds 200
}
if (-not $ready) { throw "Audio Desk is still preparing. Read $taskData\server.stdout.log and open http://127.0.0.1:$freePort/ when ready." }
$address = "http://127.0.0.1:$freePort/"
Write-Host "Audio Desk: $address"
Write-Host "Profiles, uploads and logs: $taskData"
Write-Host 'Close the browser freely; use Stop.bat to stop the local server.'
if (-not $NoBrowser) { Start-Process $address }
