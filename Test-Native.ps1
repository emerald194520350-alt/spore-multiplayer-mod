param([string]$GameExecutable)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Build Tools are required.' }
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'Visual Studio C++ build tools are required.' }
& $msbuild (Join-Path $PSScriptRoot 'tests\NativeVisualTests.vcxproj') /nologo /v:minimal /p:Configuration=Release /p:Platform=Win32
if ($LASTEXITCODE -ne 0) { throw 'Native test build failed.' }
& (Join-Path $PSScriptRoot 'bin\tests\NativeVisualTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Native visual/network tests failed.' }
if ($GameExecutable) {
    & (Join-Path $PSScriptRoot 'bin\tests\NativeVisualTests.exe') --engine-motion $GameExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Native engine movement regression tests failed.' }
}
