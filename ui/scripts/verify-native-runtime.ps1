# Verify the DLL's embedded source fingerprint before it can be staged.
param(
    [string]$Dll,
    [string]$NativeRoot,
    [string]$CMakeCommand,
    [switch]$FingerprintOnly
)
$ErrorActionPreference = 'Stop'
if (-not $NativeRoot) { $NativeRoot = Join-Path $PSScriptRoot '..\..\native' }
$nativeSourceRoot = (Resolve-Path -LiteralPath $NativeRoot).Path
if (-not $FingerprintOnly) {
    if (-not $Dll) { throw 'Specify the native DLL to verify.' }
    $runtimePath = (Resolve-Path -LiteralPath $Dll).Path
    if ((Get-Item -LiteralPath $runtimePath).Length -eq 0) { throw "Empty native runtime: $runtimePath" }
}

if (-not $CMakeCommand) {
    $availableCMake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($availableCMake) { $CMakeCommand = $availableCMake.Source }
}
if (-not $CMakeCommand) {
    foreach ($installation in @('18\Community', '18\Professional', '18\Enterprise', '2022\Community', '2022\Professional', '2022\Enterprise')) {
        $candidate = Join-Path 'C:\Program Files\Microsoft Visual Studio' "$installation\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $CMakeCommand = $candidate; break }
    }
}
if (-not $CMakeCommand) { throw 'CMake is required to verify the current native source fingerprint.' }

# Reuse the production hash policy rather than reimplementing its glob/sort.
# Configure dependency tracking has no purpose in script mode; only remove
# those tracking directives. The hash still includes the real CMakeLists.txt.
$cmakeText = [System.IO.File]::ReadAllText((Join-Path $nativeSourceRoot 'CMakeLists.txt'))
$startText = '# ECHO_NATIVE_FINGERPRINT_POLICY_BEGIN'
$endText = 'string(SHA256 ECHO_NATIVE_BUILD_HASH "${ECHO_NATIVE_BUILD_HASH}")'
$start = $cmakeText.IndexOf($startText)
$end = $cmakeText.IndexOf($endText, [Math]::Max(0, $start))
if ($start -lt 0 -or $end -lt 0) { throw 'Native source-fingerprint policy not found.' }
$hashPolicy = $cmakeText.Substring($start, $end + $endText.Length - $start)
$hashPolicy = $hashPolicy.Replace(' CONFIGURE_DEPENDS', '')
$hashPolicy = [regex]::Replace($hashPolicy, '(?m)^set_property\(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS[^\r\n]*\)\r?\n', '')
$hashPolicy = $hashPolicy.Replace('${CMAKE_CURRENT_SOURCE_DIR}', '${ECHO_IDENTITY_NATIVE_ROOT}')
$hashPolicy += "`n" + 'file(WRITE "${ECHO_IDENTITY_HASH_FILE}" "${ECHO_NATIVE_BUILD_HASH}")'

$scriptPath = Join-Path $env:TEMP ('bottlemusic-native-identity-' + [guid]::NewGuid().ToString('N') + '.cmake')
$hashPath = $scriptPath + '.hash'
try {
    [System.IO.File]::WriteAllText($scriptPath, $hashPolicy)
    & $CMakeCommand "-DECHO_IDENTITY_NATIVE_ROOT:PATH=$($nativeSourceRoot.Replace('\', '/'))" "-DECHO_IDENTITY_HASH_FILE:FILEPATH=$($hashPath.Replace('\', '/'))" -P $scriptPath
    if ($LASTEXITCODE -ne 0) { throw "Native fingerprint calculation failed (exit $LASTEXITCODE)." }
    $sourceFingerprint = [System.IO.File]::ReadAllText($hashPath).Trim()
    if ($sourceFingerprint -notmatch '^[0-9a-f]{64}$') { throw 'Invalid calculated native source fingerprint.' }
    if ($FingerprintOnly) {
        Write-Output $sourceFingerprint
        return
    }
    $runtimeBytes = [System.IO.File]::ReadAllBytes($runtimePath)
    $runtimeStrings = [System.Text.Encoding]::ASCII.GetString($runtimeBytes)
    if (-not $runtimeStrings.Contains($sourceFingerprint)) {
        throw "native_source_fingerprint_mismatch: $runtimePath does not contain current source fingerprint $sourceFingerprint. Rebuild the selected native preset before staging."
    }
    Write-Host "[native:identity] source_fingerprint=$sourceFingerprint"
    Write-Host "[native:identity] runtime=$runtimePath"
} finally {
    # These two unique files were created by this invocation; no directory cleanup.
    foreach ($temporaryFile in @($scriptPath, $hashPath)) {
        if (Test-Path -LiteralPath $temporaryFile -PathType Leaf) { Remove-Item -LiteralPath $temporaryFile -Force }
    }
}
