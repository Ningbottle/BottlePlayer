param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$NativeBuildDirectory = 'output/native-build',
    [string]$QtBuildDirectory = 'output/qt-build',
    [string]$ShaderToolsRoot = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$nativeBuild = [IO.Path]::GetFullPath((Join-Path $repoRoot $NativeBuildDirectory))
& (Join-Path $PSScriptRoot 'build-native.ps1') -BuildDirectory $NativeBuildDirectory
& (Join-Path $PSScriptRoot 'build-qt.ps1') -QtRoot $QtRoot -NativeDirectory $nativeBuild -BuildDirectory $QtBuildDirectory -ShaderToolsRoot $ShaderToolsRoot
if ($LASTEXITCODE -ne 0) { throw "Qt build failed ($LASTEXITCODE)" }
