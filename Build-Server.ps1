param([string]$OutputPath = (Join-Path $PSScriptRoot 'SporeCoop.Server.exe'))
$ErrorActionPreference = 'Stop'
$defaultOutput = Join-Path $PSScriptRoot 'SporeCoop.Server.exe'
if ([IO.Path]::GetFullPath($OutputPath) -eq $defaultOutput -and
    (Get-Process -Name 'SporeCoop.Server' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $defaultOutput })) {
    $OutputPath = Join-Path $PSScriptRoot 'SporeCoop.Server.next.exe'
}
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4 compiler is missing.' }
& $compiler /nologo /target:exe /optimize+ /platform:anycpu /reference:System.Web.Extensions.dll "/out:$OutputPath" (Join-Path $PSScriptRoot 'server\SessionServer.cs')
if ($LASTEXITCODE -ne 0) { throw 'Server compilation failed.' }
Write-Output "Built $OutputPath"
