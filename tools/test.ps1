param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$NativeBuildDirectory = 'output/native-build',
    [string]$QtBuildDirectory = 'output/qt-build',
    [switch]$IncludeAudio
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$nativeBuild = [IO.Path]::GetFullPath((Join-Path $repoRoot $NativeBuildDirectory))
$qtBuild = [IO.Path]::GetFullPath((Join-Path $repoRoot $QtBuildDirectory))
$savedPath = $env:PATH
$savedPlatform = $env:QT_QPA_PLATFORM
$savedTemp = $env:BOTTLE_TEST_TEMP_ROOT
$savedTemporary = $env:TEMP
$savedTmp = $env:TMP
$savedNativeData = $env:ECHO_NATIVE_DATA_DIR
try {
    $env:PATH = "$QtRoot/bin;$nativeBuild;$repoRoot/native/vcpkg_installed/x64-windows/bin;$savedPath"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:BOTTLE_TEST_TEMP_ROOT = Join-Path $repoRoot ('output/test-profiles/' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $env:BOTTLE_TEST_TEMP_ROOT | Out-Null
    $env:TEMP = Join-Path $env:BOTTLE_TEST_TEMP_ROOT 'temporary'
    $env:TMP = $env:TEMP
    $env:ECHO_NATIVE_DATA_DIR = Join-Path $env:BOTTLE_TEST_TEMP_ROOT 'native-default'
    New-Item -ItemType Directory -Force -Path $env:TEMP, $env:ECHO_NATIVE_DATA_DIR | Out-Null
    & ctest --test-dir $nativeBuild --output-on-failure --timeout 120 --no-tests=error
    if ($LASTEXITCODE -ne 0) { throw 'Native contract tests failed' }
    $arguments = @('--test-dir', $qtBuild, '--output-on-failure', '--timeout', '120', '--no-tests=error')
    if (!$IncludeAudio) { $arguments += @('-E', '^qt_audio_runtime$') }
    & ctest @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Qt contract tests failed' }
} finally {
    $env:PATH = $savedPath
    $env:QT_QPA_PLATFORM = $savedPlatform
    $env:BOTTLE_TEST_TEMP_ROOT = $savedTemp
    $env:TEMP = $savedTemporary
    $env:TMP = $savedTmp
    $env:ECHO_NATIVE_DATA_DIR = $savedNativeData
}
