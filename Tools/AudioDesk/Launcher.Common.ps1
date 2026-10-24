$ErrorActionPreference = 'Stop'
Import-Module Microsoft.PowerShell.Utility -Force -ErrorAction Stop
Import-Module Microsoft.PowerShell.Archive -Force -ErrorAction Stop

function Get-DeskFileSha256 {
    param([string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Resolve-DeskPath {
    param([string]$Path, [string]$Base)
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Test-DeskSamePath {
    param([string]$Left, [string]$Right)
    try { return [string]::Equals([IO.Path]::GetFullPath($Left).TrimEnd('\'), [IO.Path]::GetFullPath($Right).TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase) }
    catch { return $false }
}

function Get-DeskHealth {
    param([int]$Port, [string]$DataDirectory, [string]$AppDirectory)
    try {
        $health = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/health" -TimeoutSec 1
        if ($health.tool -ne 'MessControl Audio Desk' -or -not (Test-DeskSamePath $health.data_dir $DataDirectory)) { return $null }
        if ($AppDirectory -and -not (Test-DeskSamePath $health.app_dir $AppDirectory)) { return $null }
        return $health
    } catch { return $null }
}

function Test-DeskPortFree {
    param([int]$Port)
    $listener = $null
    try {
        $listener = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, $Port)
        $listener.Start()
        return $true
    } catch { return $false }
    finally { if ($listener) { $listener.Stop() } }
}

function Get-DeskManifest {
    param([string]$Root, [string]$ManifestName)
    $path = Join-Path $Root $ManifestName
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $ManifestName. Download the complete AudioDesk tool folder." }
    $manifest = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.schema_version -ne 1 -or $manifest.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw "Invalid $ManifestName." }
    if (-not $manifest.archive -or [IO.Path]::GetFileName($manifest.archive) -ne $manifest.archive) { throw "Invalid archive filename in $ManifestName." }
    return $manifest
}

function Test-DeskLfsPointer {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $true }
    if ((Get-Item -LiteralPath $Path).Length -gt 4096) { return $false }
    return (Get-Content -LiteralPath $Path -TotalCount 1 -ErrorAction SilentlyContinue) -like 'version https://git-lfs.github.com/spec/*'
}

function Resolve-DeskArchive {
    param([string]$Root, $Manifest, [switch]$NoDownload)
    $archive = Join-Path $Root $Manifest.archive
    if (Test-DeskLfsPointer $archive) {
        if ($NoDownload) { throw "The real $($Manifest.archive) is missing (possibly a Git LFS pointer). Run git lfs pull --include=Tools/AudioDesk/runtime.zip,Tools/AudioDesk/library.zip --exclude=" }
        $git = Get-Command git -ErrorAction SilentlyContinue
        $repo = [IO.Path]::GetFullPath((Join-Path $Root '..\..'))
        if ($git -and (Test-Path -LiteralPath (Join-Path $repo '.git'))) {
            Write-Host 'Downloading Audio Desk bundles with Git LFS...'
            $oldPreference = $ErrorActionPreference
            try {
                $ErrorActionPreference = 'Continue'
                & $git.Source -C $repo lfs pull '--include=Tools/AudioDesk/runtime.zip,Tools/AudioDesk/library.zip' '--exclude=' 2>&1 | ForEach-Object { Write-Host $_ }
            } finally { $ErrorActionPreference = $oldPreference }
        }
        if (Test-DeskLfsPointer $archive) {
            if (-not $Manifest.download_url -or $Manifest.download_url -notmatch '^https://') { throw "The real $($Manifest.archive) is missing. Run git lfs pull for Tools/AudioDesk, or download the complete application bundle." }
            $temporary = Join-Path $Root ($Manifest.archive + '.' + [Guid]::NewGuid().ToString('N') + '.tmp')
            try {
                Write-Host "Downloading $($Manifest.archive)..."
                [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
                # Windows PowerShell 5.1 progress updates make large downloads
                # needlessly slow, including when the launcher output is piped.
                $ProgressPreference = 'SilentlyContinue'
                Invoke-WebRequest -Uri $Manifest.download_url -OutFile $temporary -UseBasicParsing -TimeoutSec 300
                $downloadHash = Get-DeskFileSha256 $temporary
                if ($downloadHash -ne $Manifest.sha256) { throw "SHA256 mismatch for downloaded $($Manifest.archive)." }
                Move-Item -LiteralPath $temporary -Destination $archive -Force
            } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force } }
        }
    }
    $actual = Get-DeskFileSha256 $archive
    if ($actual -ne $Manifest.sha256) { throw "SHA256 mismatch for $($Manifest.archive). Restore this archive from the same repository version as its manifest." }
    return $archive
}

function Remove-DeskStage {
    param([string]$Path, [string]$Root)
    $absolute = [IO.Path]::GetFullPath($Path)
    $parent = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $name = [IO.Path]::GetFileName($absolute)
    if (-not $absolute.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or $name -notmatch '^\.(runtime|library)\.(stage|previous)\.[0-9a-f]+$') { throw 'Refusing to remove an unexpected staging path.' }
    if (Test-Path -LiteralPath $absolute) { Remove-Item -LiteralPath $absolute -Recurse -Force }
}

function Install-DeskBundle {
    param([string]$Root, [string]$ManifestName, [string]$DestinationName, [string]$ArchiveRoot = '', [switch]$NoDownload)
    $Root = [IO.Path]::GetFullPath($Root)
    $manifest = Get-DeskManifest $Root $ManifestName
    $destination = Join-Path $Root $DestinationName
    $marker = Join-Path $destination '.manifest-sha256.txt'
    $cached = (Test-Path -LiteralPath $marker -PathType Leaf) -and ((Get-Content -LiteralPath $marker -Raw).Trim() -eq $manifest.sha256)
    if ($cached -and $DestinationName -eq '.runtime') { $cached = Test-Path -LiteralPath (Join-Path $destination $manifest.app_relative_path) -PathType Leaf }
    if ($cached) { return $destination }
    if ((Test-Path -LiteralPath $destination) -and -not (Test-Path -LiteralPath $marker -PathType Leaf)) { throw "$DestinationName already exists without an Audio Desk marker. Keep your files and rename that folder before installing the bundled copy." }
    $archive = Resolve-DeskArchive $Root $manifest -NoDownload:$NoDownload
    $kind = if ($DestinationName -eq '.runtime') { 'runtime' } else { 'library' }
    $stage = Join-Path $Root ('.' + $kind + '.stage.' + [Guid]::NewGuid().ToString('N'))
    $previous = $null
    try {
        New-Item -ItemType Directory -Path $stage -Force | Out-Null
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::OpenRead($archive)
        try {
            $stagePrefix = [IO.Path]::GetFullPath($stage).TrimEnd('\') + '\'
            foreach ($entry in $zip.Entries) {
                $target = [IO.Path]::GetFullPath((Join-Path $stage $entry.FullName.Replace('/', '\')))
                if (-not $target.StartsWith($stagePrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe path in the application archive.' }
            }
        } finally { $zip.Dispose() }
        Expand-Archive -LiteralPath $archive -DestinationPath $stage -Force
        $prepared = if ($ArchiveRoot) { Join-Path $stage $ArchiveRoot } else { $stage }
        if (-not (Test-Path -LiteralPath $prepared -PathType Container)) { throw 'Archive folder layout does not match the manifest.' }
        if ($DestinationName -eq '.runtime') {
            $relative = [string]$manifest.app_relative_path
            if (-not $relative -or [IO.Path]::IsPathRooted($relative)) { throw 'Missing application path in the runtime manifest.' }
            $exe = [IO.Path]::GetFullPath((Join-Path $prepared $relative))
            if (-not $exe.StartsWith(([IO.Path]::GetFullPath($prepared).TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'The runtime executable is missing or has an unsafe path.' }
        }
        Set-Content -LiteralPath (Join-Path $prepared '.manifest-sha256.txt') -Value $manifest.sha256 -Encoding ASCII
        if (Test-Path -LiteralPath $destination) {
            $previous = Join-Path $Root ('.' + $kind + '.previous.' + [Guid]::NewGuid().ToString('N'))
            Move-Item -LiteralPath $destination -Destination $previous
        }
        try { Move-Item -LiteralPath $prepared -Destination $destination }
        catch {
            if ($previous -and -not (Test-Path -LiteralPath $destination)) { Move-Item -LiteralPath $previous -Destination $destination }
            throw
        }
        if ($previous -and $kind -eq 'runtime') { Remove-DeskStage $previous $Root }
        elseif ($previous) { Write-Host "Previous library retained: $previous" }
        return $destination
    } finally { if (Test-Path -LiteralPath $stage) { Remove-DeskStage $stage $Root } }
}
