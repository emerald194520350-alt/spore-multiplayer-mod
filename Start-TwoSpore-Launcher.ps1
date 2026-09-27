$ErrorActionPreference = 'Stop'

$logDirectory = Join-Path $env:LOCALAPPDATA 'SporeCoop'
$logPath = Join-Path $logDirectory 'launcher.log'
$startScript = Join-Path $PSScriptRoot 'Start-TwoSpore.ps1'

New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

try {
    "`r`n[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] Starting two SPORE windows" |
        Out-File -LiteralPath $logPath -Encoding utf8 -Append
    & $startScript *>&1 | Out-File -LiteralPath $logPath -Encoding utf8 -Append
}
catch {
    $details = $_ | Out-String
    $details | Out-File -LiteralPath $logPath -Encoding utf8 -Append

    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show(
        "Could not start two SPORE windows.`n`n$($_.Exception.Message)`n`nDetailed log:`n$logPath",
        'SPORE Coop — launch error',
        [System.Windows.MessageBoxButton]::OK,
        [System.Windows.MessageBoxImage]::Error) | Out-Null
    exit 1
}
