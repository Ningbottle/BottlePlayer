# Pester-free contract check for sync-backend.ps1.
# Verifies preset-exact source selection, no silent fallback, and SHA-256 printing.
$ErrorActionPreference = 'Stop'

$syncScript = Join-Path $PSScriptRoot 'sync-backend.ps1'
if (-not (Test-Path -LiteralPath $syncScript)) {
    throw "sync-backend.ps1 not found next to verifier: $syncScript"
}

$failures = [System.Collections.Generic.List[string]]::new()
function Assert-True {
    param([bool]$Condition, [string]$Message)
    if ($Condition) {
        Write-Host "PASS: $Message"
    } else {
        $script:failures.Add($Message) | Out-Null
        Write-Host "FAIL: $Message"
    }
}

function New-DummyFile {
    param([string]$Path, [string]$Content)
    $dir = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [System.IO.File]::WriteAllText($Path, $Content)
}

function Invoke-SyncInSandbox {
    param(
        [string]$SandboxRoot,
        [string]$Preset,
        [switch]$ExpectFailure
    )
    $sandboxSync = Join-Path $SandboxRoot 'ui\scripts\sync-backend.ps1'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sandboxSync) | Out-Null
    Copy-Item -LiteralPath $syncScript -Destination $sandboxSync -Force

    $outFile = Join-Path $SandboxRoot 'sync-out.txt'
    $errFile = Join-Path $SandboxRoot 'sync-err.txt'
    $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $sandboxSync,
        '-Preset', $Preset
    ) -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile -WindowStyle Hidden

    $stdout = Get-Content -LiteralPath $outFile -Raw -ErrorAction SilentlyContinue
    $stderr = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
    if ($null -eq $stdout) { $stdout = '' }
    if ($null -eq $stderr) { $stderr = '' }
    $combined = $stdout + "`n" + $stderr

    if ($ExpectFailure) {
        Assert-True ($proc.ExitCode -ne 0) "preset=$Preset missing source must fail (exit=$($proc.ExitCode))"
    } else {
        Assert-True ($proc.ExitCode -eq 0) "preset=$Preset sync must succeed (exit=$($proc.ExitCode) err=$stderr)"
    }
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        Output   = $combined
    }
}

function Get-Sha256 {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-FileText {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return Get-Content -LiteralPath $Path -Raw
}

# --- Case 1: check preset copies only from check ---
$sandbox1 = Join-Path $env:TEMP ("sync-verify-check-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $sandbox1 | Out-Null
try {
    New-DummyFile (Join-Path $sandbox1 'native\out\bottlemusic-check\EchoCAPI.dll') 'CHECK-DLL-CONTENT-AAAA'
    New-DummyFile (Join-Path $sandbox1 'native\out\bottlemusic-release\EchoCAPI.dll') 'RELEASE-DLL-CONTENT-BBBB'
    New-DummyFile (Join-Path $sandbox1 'native\vcpkg_installed\x64-windows\bin\sqlite3.dll') 'SQLITE-DLL'
    New-DummyFile (Join-Path $sandbox1 'ui\src-tauri\target\release\EchoCAPI.dll') 'OLD-RELEASE-MUST-NOT-CHANGE'
    New-Item -ItemType Directory -Force -Path (Join-Path $sandbox1 'ui\src-tauri\libs') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $sandbox1 'ui\src-tauri\target\debug') | Out-Null

    $result = Invoke-SyncInSandbox -SandboxRoot $sandbox1 -Preset 'bottlemusic-check'
    $checkHash = Get-Sha256 (Join-Path $sandbox1 'native\out\bottlemusic-check\EchoCAPI.dll')
    $releaseHash = Get-Sha256 (Join-Path $sandbox1 'native\out\bottlemusic-release\EchoCAPI.dll')
    $staging = Join-Path $sandbox1 'ui\src-tauri\libs\EchoCAPI.dll'
    $debugDst = Join-Path $sandbox1 'ui\src-tauri\target\debug\EchoCAPI.dll'
    $releaseDst = Join-Path $sandbox1 'ui\src-tauri\target\release\EchoCAPI.dll'

    Assert-True (Test-Path -LiteralPath $staging) 'check sync writes staging EchoCAPI.dll'
    Assert-True (Test-Path -LiteralPath $debugDst) 'check sync writes target/debug EchoCAPI.dll'
    Assert-True ((Get-Sha256 $staging) -eq $checkHash) 'check staging hash equals check source'
    Assert-True ((Get-Sha256 $debugDst) -eq $checkHash) 'check debug hash equals check source'
    Assert-True ((Get-Sha256 $staging) -ne $releaseHash) 'check staging hash is not release hash'
    Assert-True ((Get-FileText $releaseDst) -eq 'OLD-RELEASE-MUST-NOT-CHANGE') 'check sync must not overwrite target/release'
    Assert-True ($result.Output -match [regex]::Escape((Join-Path $sandbox1 'native\out\bottlemusic-check\EchoCAPI.dll'))) 'check output prints source absolute path'
    Assert-True ($result.Output -match $checkHash) 'check output prints source SHA-256'
    $sqliteDebug = Join-Path $sandbox1 'ui\src-tauri\target\debug\sqlite3.dll'
    $sqliteRelease = Join-Path $sandbox1 'ui\src-tauri\target\release\sqlite3.dll'
    Assert-True (Test-Path -LiteralPath $sqliteDebug) 'check sync copies sqlite3.dll to debug profile'
    Assert-True (-not (Test-Path -LiteralPath $sqliteRelease)) 'check sync must not copy sqlite3.dll to release profile'
}
finally {
    Remove-Item -LiteralPath $sandbox1 -Recurse -Force -ErrorAction SilentlyContinue
}

# --- Case 2: release preset copies only from release ---
$sandbox2 = Join-Path $env:TEMP ("sync-verify-rel-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $sandbox2 | Out-Null
try {
    New-DummyFile (Join-Path $sandbox2 'native\out\bottlemusic-check\EchoCAPI.dll') 'CHECK-DLL-CONTENT-AAAA'
    New-DummyFile (Join-Path $sandbox2 'native\out\bottlemusic-release\EchoCAPI.dll') 'RELEASE-DLL-CONTENT-BBBB'
    New-DummyFile (Join-Path $sandbox2 'native\vcpkg_installed\x64-windows\bin\sqlite3.dll') 'SQLITE-DLL'
    New-DummyFile (Join-Path $sandbox2 'ui\src-tauri\target\debug\EchoCAPI.dll') 'OLD-DEBUG-MUST-NOT-CHANGE'
    New-Item -ItemType Directory -Force -Path (Join-Path $sandbox2 'ui\src-tauri\libs') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $sandbox2 'ui\src-tauri\target\release') | Out-Null

    $result = Invoke-SyncInSandbox -SandboxRoot $sandbox2 -Preset 'bottlemusic-release'
    $checkHash = Get-Sha256 (Join-Path $sandbox2 'native\out\bottlemusic-check\EchoCAPI.dll')
    $releaseHash = Get-Sha256 (Join-Path $sandbox2 'native\out\bottlemusic-release\EchoCAPI.dll')
    $staging = Join-Path $sandbox2 'ui\src-tauri\libs\EchoCAPI.dll'
    $debugDst = Join-Path $sandbox2 'ui\src-tauri\target\debug\EchoCAPI.dll'
    $releaseDst = Join-Path $sandbox2 'ui\src-tauri\target\release\EchoCAPI.dll'

    Assert-True (Test-Path -LiteralPath $staging) 'release sync writes staging EchoCAPI.dll'
    Assert-True (Test-Path -LiteralPath $releaseDst) 'release sync writes target/release EchoCAPI.dll'
    Assert-True ((Get-Sha256 $staging) -eq $releaseHash) 'release staging hash equals release source'
    Assert-True ((Get-Sha256 $releaseDst) -eq $releaseHash) 'release dest hash equals release source'
    Assert-True ((Get-Sha256 $staging) -ne $checkHash) 'release staging hash is not check hash'
    Assert-True ((Get-FileText $debugDst) -eq 'OLD-DEBUG-MUST-NOT-CHANGE') 'release sync must not overwrite target/debug'
    Assert-True ($result.Output -match [regex]::Escape((Join-Path $sandbox2 'native\out\bottlemusic-release\EchoCAPI.dll'))) 'release output prints source absolute path'
    Assert-True ($result.Output -match $releaseHash) 'release output prints source SHA-256'
    $sqliteDebug = Join-Path $sandbox2 'ui\src-tauri\target\debug\sqlite3.dll'
    $sqliteRelease = Join-Path $sandbox2 'ui\src-tauri\target\release\sqlite3.dll'
    Assert-True (Test-Path -LiteralPath $sqliteRelease) 'release sync copies sqlite3.dll to release profile'
    Assert-True (-not (Test-Path -LiteralPath $sqliteDebug)) 'release sync must not copy sqlite3.dll to debug profile'
}
finally {
    Remove-Item -LiteralPath $sandbox2 -Recurse -Force -ErrorAction SilentlyContinue
}

# --- Case 3: specified preset missing must fail, no silent fallback ---
$sandbox3 = Join-Path $env:TEMP ("sync-verify-miss-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $sandbox3 | Out-Null
try {
    New-DummyFile (Join-Path $sandbox3 'native\out\bottlemusic-check\EchoCAPI.dll') 'CHECK-ONLY'
    New-Item -ItemType Directory -Force -Path (Join-Path $sandbox3 'ui\src-tauri\libs') | Out-Null
    $staging = Join-Path $sandbox3 'ui\src-tauri\libs\EchoCAPI.dll'

    $result = Invoke-SyncInSandbox -SandboxRoot $sandbox3 -Preset 'bottlemusic-release' -ExpectFailure
    Assert-True (-not (Test-Path -LiteralPath $staging)) 'missing release source must not copy check DLL into staging'
    Assert-True ($result.Output -match 'bottlemusic-release' -or $result.ExitCode -ne 0) 'missing release source error mentions preset or non-zero exit'
}
finally {
    Remove-Item -LiteralPath $sandbox3 -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host "verify-sync-backend: $($failures.Count) failed"
    foreach ($f in $failures) { Write-Host "  - $f" }
    exit 1
}
Write-Host 'verify-sync-backend: all passed'
exit 0
