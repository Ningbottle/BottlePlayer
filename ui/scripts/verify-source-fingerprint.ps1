# Exercise the actual source-fingerprint block in a temporary CMake project.
param(
    [string]$WorkDir,
    [string]$CMakeFile = (Join-Path $PSScriptRoot '..\..\native\CMakeLists.txt')
)
$ErrorActionPreference = 'Stop'
if (-not $WorkDir) { throw 'Specify a fresh WorkDir for the controlled fixture.' }
$fixtureRoot = [System.IO.Path]::GetFullPath($WorkDir)
if (Test-Path -LiteralPath $fixtureRoot) { throw "Fixture already exists: $fixtureRoot" }
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null

$nativeCMake = Get-Content -LiteralPath $CMakeFile -Raw
$startText = 'set(ECHO_NATIVE_SOURCE_DIRS'
$endText = 'string(SHA256 ECHO_NATIVE_BUILD_HASH "${ECHO_NATIVE_BUILD_HASH}")'
$start = $nativeCMake.IndexOf($startText)
if ($start -lt 0) { throw 'Native source-fingerprint block not found (start marker).' }
$end = $nativeCMake.IndexOf($endText, $start)
if ($end -lt 0) { throw 'Native source-fingerprint block not found (end marker).' }
$block = $nativeCMake.Substring($start, $end + $endText.Length - $start)
$project = "cmake_minimum_required(VERSION 3.24)`nproject(SourceFingerprint NONE)`n" + $block + @'

file(WRITE "${CMAKE_BINARY_DIR}/fingerprint.txt" "${ECHO_NATIVE_BUILD_HASH}")
add_custom_target(check_hash ALL COMMAND ${CMAKE_COMMAND} -E echo "fingerprint=${ECHO_NATIVE_BUILD_HASH}")
'@
[System.IO.File]::WriteAllText((Join-Path $fixtureRoot 'CMakeLists.txt'), $project)
# The fingerprint block globs the enumerated source directories, so the
# fixture files must live inside one of them (core) to be seen at all.
$fixtureDir = Join-Path $fixtureRoot 'core'
New-Item -ItemType Directory -Path $fixtureDir | Out-Null
$header = Join-Path $fixtureDir 'fixture.h'
[System.IO.File]::WriteAllText($header, '// first source version')
$buildDir = Join-Path $fixtureRoot 'out'
$buildLog = Join-Path $fixtureRoot 'build-log.txt'
[System.IO.File]::WriteAllText($buildLog, '')
function Invoke-FixtureCMake {
    param([string[]]$Arguments)
    $previousPreference = $ErrorActionPreference
    try {
        # CMake's successful glob recheck writes a diagnostic to stderr.
        # Windows PowerShell wraps it as NativeCommandError; the exit code,
        # rather than the stream used for a diagnostic, decides success.
        $ErrorActionPreference = 'Continue'
        & cmake @Arguments *>> $buildLog
        $nativeExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($nativeExit -ne 0) { throw "CMake fixture failed: $nativeExit" }
}
Invoke-FixtureCMake -Arguments @('-S', $fixtureRoot, '-B', $buildDir, '-G', 'NMake Makefiles')

function Read-Fingerprint { return [System.IO.File]::ReadAllText((Join-Path $buildDir 'fingerprint.txt')) }
function Build-Fixture {
    Invoke-FixtureCMake -Arguments @('--build', $buildDir)
}
$initialHash = Read-Fingerprint
[System.IO.File]::WriteAllText($header, '// changed existing source contents')
Build-Fixture
$changedHash = Read-Fingerprint
$newSource = Join-Path $fixtureDir 'added.cpp'
[System.IO.File]::WriteAllText($newSource, '// a newly added source')
Build-Fixture
$addedHash = Read-Fingerprint
Remove-Item -LiteralPath $newSource
Build-Fixture
$removedHash = Read-Fingerprint

$result = [ordered]@{
    changed_content_refreshes = ($initialHash -ne $changedHash)
    added_source_refreshes = ($changedHash -ne $addedHash)
    removed_source_refreshes = ($addedHash -ne $removedHash)
    removal_restores_previous_hash = ($removedHash -eq $changedHash)
    initial = $initialHash
    changed = $changedHash
    added = $addedHash
    removed = $removedHash
}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixtureRoot 'result.json') -Encoding UTF8
$result | ConvertTo-Json | Write-Output
if (-not $result.changed_content_refreshes -or -not $result.added_source_refreshes -or
    -not $result.removed_source_refreshes -or -not $result.removal_restores_previous_hash) { exit 1 }
