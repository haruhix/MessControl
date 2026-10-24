param(
    [string]$Python = 'python',
    [string]$MediaDirectory = '',
    [string]$Version = '1.1.0',
    [string]$DownloadBaseUrl = '',
    [switch]$RepackOnly
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$taskBuild = Join-Path $taskRoot '.build'
$taskTemp = Join-Path $taskBuild 'temp'
New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$env:TEMP = $taskTemp
$env:TMP = $taskTemp
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONPYCACHEPREFIX = Join-Path $taskBuild 'pycache'
$env:PYINSTALLER_CONFIG_DIR = Join-Path $taskBuild 'pyinstaller-cache'
if (-not $RepackOnly) {
& $Python -m PyInstaller --version
if ($LASTEXITCODE -ne 0) { throw 'Install PyInstaller in the developer Python environment before building.' }
$arguments = @('-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir', '--console', '--name', 'AudioDesk',
    '--distpath', (Join-Path $taskBuild 'dist'), '--workpath', (Join-Path $taskBuild 'work'),
    '--specpath', (Join-Path $taskBuild 'spec'), '--add-data', ((Join-Path $taskRoot 'web') + ';web'),
    '--add-data', ((Join-Path $taskRoot 'catalog.json') + ';.'))
foreach ($name in @('seed_library.json', 'seed_profile.json', 'THIRD_PARTY_AUDIO.md')) {
    $source = Join-Path $taskRoot $name
    if (Test-Path -LiteralPath $source -PathType Leaf) { $arguments += @('--add-data', ($source + ';.')) }
}
$notices = Join-Path $taskRoot 'ThirdParty'
if (Test-Path -LiteralPath $notices -PathType Container) { $arguments += @('--add-data', ($notices + ';ThirdParty')) }
if ($MediaDirectory) {
    $mediaRoot = [IO.Path]::GetFullPath($MediaDirectory)
    if (-not (Test-Path -LiteralPath (Join-Path $mediaRoot 'ffmpeg.exe') -PathType Leaf)) { throw 'MediaDirectory must contain ffmpeg.exe.' }
    if (-not (Test-Path -LiteralPath (Join-Path $mediaRoot 'ffprobe.exe') -PathType Leaf)) { throw 'MediaDirectory must contain ffprobe.exe.' }
    foreach ($item in Get-ChildItem -LiteralPath $mediaRoot -File | Where-Object { $_.Extension -in @('.exe', '.dll') }) {
        $arguments += @('--add-binary', ($item.FullName + ';media'))
    }
}
$arguments += (Join-Path $taskRoot 'app.py')
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw 'PyInstaller failed; runtime.zip was not replaced.' }
}
$application = Join-Path $taskBuild 'dist\AudioDesk'
if (-not (Test-Path -LiteralPath (Join-Path $application 'AudioDesk.exe') -PathType Leaf)) { throw 'The built executable is missing.' }
$applicationPrefix = [IO.Path]::GetFullPath($application).TrimEnd('\') + '\'
$media = Join-Path $application '_internal\media'
foreach ($mediaDll in Get-ChildItem -LiteralPath $media -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^(avcodec|avdevice|avfilter|avformat|avutil|swresample|swscale)-[0-9]+\.dll$' }) {
    $name = $mediaDll.Name
    $duplicate = [IO.Path]::GetFullPath((Join-Path $application ('_internal\' + $name)))
    $preserved = Join-Path $media $name
    if (-not $duplicate.StartsWith($applicationPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected duplicate DLL path.' }
    if ((Test-Path -LiteralPath $duplicate -PathType Leaf) -and (Test-Path -LiteralPath $preserved -PathType Leaf)) {
        if ((Get-FileHash -LiteralPath $duplicate -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $preserved -Algorithm SHA256).Hash) {
            Remove-Item -LiteralPath $duplicate -Force
        }
    }
}
$temporaryZip = Join-Path $taskBuild ('runtime-' + [Guid]::NewGuid().ToString('N') + '.zip')
Compress-Archive -LiteralPath $application -DestinationPath $temporaryZip -CompressionLevel Optimal
$archive = Join-Path $taskRoot 'runtime.zip'
Move-Item -LiteralPath $temporaryZip -Destination $archive -Force
$manifest = [ordered]@{
    schema_version = 1
    archive = 'runtime.zip'
    sha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    bytes = (Get-Item -LiteralPath $archive).Length
    app_relative_path = 'AudioDesk/AudioDesk.exe'
    version = $Version
    platform = 'windows-x64'
}
if ($DownloadBaseUrl) { $manifest.download_url = $DownloadBaseUrl.TrimEnd('/') + '/runtime.zip' }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskRoot 'runtime-manifest.json') -Encoding UTF8
Write-Host "Built: $archive"
Write-Host "SHA256: $($manifest.sha256)"
Write-Host "Bytes: $($manifest.bytes)"
