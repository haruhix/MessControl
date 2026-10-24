param([string]$TestDirectory = 'E:\CodexTemp\AudioDeskPortable\LauncherQA')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Launcher.Common.ps1')
$taskTestRoot = [IO.Path]::GetFullPath($TestDirectory)
if (-not $taskTestRoot.StartsWith('E:\CodexTemp\AudioDeskPortable\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Tests must use the explicit E: task temp directory.' }
$taskRun = Join-Path $taskTestRoot ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskRun -Force | Out-Null
$env:TEMP = $taskRun
$env:TMP = $taskRun
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$passed = 0

function Assert-Desk { param([bool]$Value,[string]$Message) if (-not $Value) { throw $Message } }
function Assert-DeskThrows { param([scriptblock]$Action,[string]$Message) $threw=$false; try { & $Action | Out-Null } catch { $threw=$true }; Assert-Desk $threw $Message }
function Test-DeskCase { param([string]$Name,[scriptblock]$Action) & $Action; $script:passed++; Write-Host "PASS $Name" }
function New-TestBundle {
    param([string]$Name,[string]$Entry='library/clip.txt')
    $root = Join-Path $taskRun $Name
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $archive = Join-Path $root 'test.zip'
    $zip = [IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
    try {
        $zipEntry = $zip.CreateEntry($Entry)
        $writer = New-Object IO.StreamWriter($zipEntry.Open())
        try { $writer.Write('test audio placeholder') } finally { $writer.Dispose() }
    } finally { $zip.Dispose() }
    $manifest = [ordered]@{schema_version=1;archive='test.zip';sha256=(Get-DeskFileSha256 $archive);bytes=(Get-Item -LiteralPath $archive).Length}
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'test-manifest.json') -Encoding UTF8
    return $root
}

Test-DeskCase 'PowerShell syntax' {
    foreach ($name in @('Start.ps1','Stop.ps1','Launcher.Common.ps1','Build.ps1')) {
        $tokens=$null;$parseErrors=$null
        $null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name),[ref]$tokens,[ref]$parseErrors)
        Assert-Desk ($parseErrors.Count -eq 0) "Parse failure: $name"
    }
}
Test-DeskCase 'Batch files find PowerShell without PATH' {
    foreach ($name in @('Start.bat','Stop.bat')) {
        $body=Get-Content -LiteralPath (Join-Path $PSScriptRoot $name) -Raw
        Assert-Desk ($body.Contains('"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"')) "Missing absolute PowerShell path: $name"
        Assert-Desk ($body.Contains('"%~dp0')) "Missing quoted script-root path: $name"
    }
}
Test-DeskCase 'Relative and Unicode paths' {
    $unicode = 'folder ' + [char]0x0417 + [char]0x0432 + [char]0x0443 + [char]0x043A
    $root=Join-Path $taskRun $unicode
    Assert-Desk (Test-DeskSamePath (Resolve-DeskPath 'child\..\data' $root) (Join-Path $root 'data')) 'Relative Unicode resolution changed the path.'
    Assert-Desk (-not (Test-DeskSamePath $root ($root+'other'))) 'Path equality accepted a prefix.'
}
Test-DeskCase 'Archive and manifest hash validation' {
    $root=New-TestBundle 'hash'
    $manifest=Get-DeskManifest $root 'test-manifest.json'
    $archive=Resolve-DeskArchive $root $manifest -NoDownload
    Assert-Desk (Test-Path -LiteralPath $archive) 'Valid archive was rejected.'
    $manifest.sha256=('0'*64)
    Assert-DeskThrows { Resolve-DeskArchive $root $manifest -NoDownload } 'Corrupt archive hash was accepted.'
    $manifest.archive='../test.zip'
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'bad.json') -Encoding UTF8
    Assert-DeskThrows { Get-DeskManifest $root 'bad.json' } 'Manifest archive traversal was accepted.'
}
Test-DeskCase 'LFS pointer gives explicit offline failure' {
    $root=New-TestBundle 'pointer'
    $manifest=Get-DeskManifest $root 'test-manifest.json'
    Set-Content -LiteralPath (Join-Path $root 'test.zip') -Value 'version https://git-lfs.github.com/spec/v1' -Encoding ASCII
    Assert-Desk (Test-DeskLfsPointer (Join-Path $root 'test.zip')) 'LFS pointer not detected.'
    Assert-DeskThrows { Resolve-DeskArchive $root $manifest -NoDownload } 'Offline pointer was accepted as archive.'
}
Test-DeskCase 'Library extraction, cache and managed update preserve old copy' {
    $root=New-TestBundle 'library'
    $destination=Install-DeskBundle $root 'test-manifest.json' 'library' -ArchiveRoot 'library' -NoDownload
    Assert-Desk (Test-Path -LiteralPath (Join-Path $destination 'clip.txt')) 'Library did not extract.'
    Set-Content -LiteralPath (Join-Path $destination 'personal.txt') -Value 'keep' -Encoding ASCII
    $again=Install-DeskBundle $root 'test-manifest.json' 'library' -ArchiveRoot 'library' -NoDownload
    Assert-Desk (Test-DeskSamePath $destination $again) 'Cached bundle path changed.'
    Set-Content -LiteralPath (Join-Path $destination '.manifest-sha256.txt') -Value ('0'*64) -Encoding ASCII
    $null=Install-DeskBundle $root 'test-manifest.json' 'library' -ArchiveRoot 'library' -NoDownload
    $old=@(Get-ChildItem -LiteralPath $root -Directory -Filter '.library.previous.*')
    Assert-Desk ($old.Count -eq 1 -and (Test-Path -LiteralPath (Join-Path $old[0].FullName 'personal.txt'))) 'Old library copy was not preserved.'
}
Test-DeskCase 'Unmanaged library is preserved' {
    $root=New-TestBundle 'unmanaged'
    $destination=Join-Path $root 'library'
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $personal=Join-Path $destination 'mine.txt'
    Set-Content -LiteralPath $personal -Value 'keep' -Encoding ASCII
    Assert-DeskThrows { Install-DeskBundle $root 'test-manifest.json' 'library' -ArchiveRoot 'library' -NoDownload } 'Unmanaged library was overwritten.'
    Assert-Desk ((Get-Content -LiteralPath $personal -Raw).Trim() -eq 'keep') 'Unmanaged content changed.'
}
Test-DeskCase 'ZIP traversal cannot escape staging' {
    $root=New-TestBundle 'traversal' '../escaped.txt'
    Assert-DeskThrows { Install-DeskBundle $root 'test-manifest.json' 'library' -ArchiveRoot 'library' -NoDownload } 'Archive traversal was accepted.'
    Assert-Desk (-not (Test-Path -LiteralPath (Join-Path $root 'escaped.txt'))) 'Traversal wrote outside staging.'
    Assert-Desk (@(Get-ChildItem -LiteralPath $root -Directory -Filter '.library.stage.*').Count -eq 0) 'Failed staging was left behind.'
}
Test-DeskCase 'Recursive staging cleanup rejects unrelated directories' {
    $root=New-TestBundle 'cleanup'
    Assert-DeskThrows { Remove-DeskStage $root $taskRun } 'Cleanup accepted an unrelated directory.'
    Assert-DeskThrows { Remove-DeskStage (Join-Path $taskRun '.library.stage.abc') $root } 'Cleanup accepted a path outside root.'
    Assert-Desk (Test-Path -LiteralPath $root) 'Unrelated directory was removed.'
}
Write-Host "$passed launcher checks passed. Artifacts: $taskRun"
