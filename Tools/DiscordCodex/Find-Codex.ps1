function Find-LocalCodex([string]$PreferredPath) {
    if ($PreferredPath -and (Test-Path -LiteralPath $PreferredPath -PathType Leaf)) {
        return [IO.Path]::GetFullPath($PreferredPath)
    }
    $taskCommand = Get-Command codex.exe -ErrorAction SilentlyContinue
    if ($taskCommand -and (Test-Path -LiteralPath $taskCommand.Source -PathType Leaf)) { return $taskCommand.Source }
    # Explorer-launched terminals may not have the desktop app's injected PATH.
    $taskBinRoot = Join-Path $env:LOCALAPPDATA 'OpenAI\Codex\bin'
    if (Test-Path -LiteralPath $taskBinRoot) {
        $taskCandidates = @(Get-ChildItem -LiteralPath $taskBinRoot -Directory | ForEach-Object {
            $taskCandidatePath = Join-Path $_.FullName 'codex.exe'
            if (Test-Path -LiteralPath $taskCandidatePath -PathType Leaf) { Get-Item -LiteralPath $taskCandidatePath }
        } | Sort-Object LastWriteTime -Descending)
        if ($taskCandidates.Count -gt 0) { return $taskCandidates[0].FullName }
    }
    throw 'Codex executable not found. Install the Codex desktop app or make codex.exe available in PATH.'
}
