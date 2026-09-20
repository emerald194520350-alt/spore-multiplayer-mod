param([string]$NodePath)
$ErrorActionPreference = 'Stop'
if (-not $NodePath) {
    $nodeCommand = Get-Command node -ErrorAction SilentlyContinue
    if ($nodeCommand) { $NodePath = $nodeCommand.Source }
    else {
        $runtimeRoot = Join-Path $env:LOCALAPPDATA 'OpenAI\Codex\runtimes'
        $NodePath = Get-ChildItem -LiteralPath $runtimeRoot -Recurse -File -Filter node.exe -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $NodePath) { throw 'Node.js 22 or later is required to run the protocol tests.' }
$testServer = Join-Path $PSScriptRoot 'obj\tests\SporeCoop.Server.test.exe'
New-Item -ItemType Directory -Path (Split-Path -Parent $testServer) -Force | Out-Null
& (Join-Path $PSScriptRoot 'Build-Server.ps1') -OutputPath $testServer
$previousTestServer = $env:SPORE_COOP_TEST_SERVER
try {
    $env:SPORE_COOP_TEST_SERVER = $testServer
    & $NodePath (Join-Path $PSScriptRoot 'tests\server.test.mjs')
    if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed.' }
}
finally { $env:SPORE_COOP_TEST_SERVER = $previousTestServer }
