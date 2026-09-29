# Sync native runtime DLLs for Rust libloading.
# Usage: pnpm backend:sync
#        powershell -File ./scripts/sync-backend.ps1 -Preset bottlemusic-release
# Selected preset is the only allowed EchoCAPI source; missing source fails
# instead of silently falling back to the other preset.
param(
    [ValidateSet('bottlemusic-check', 'bottlemusic-release')]
    [string]$Preset = 'bottlemusic-check'
)
$ErrorActionPreference = 'Stop'

# Windows PowerShell can inherit PowerShell 7 module paths from a parent pwsh
# process and then fail to auto-load its own Get-FileHash implementation.
if (-not (Get-Command Get-FileHash -ErrorAction SilentlyContinue)) {
    $utilityModule = Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1'
    Import-Module $utilityModule -Force -ErrorAction Stop
}

$uiRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$nativeOutDll = Join-Path $uiRoot "..\native\out\$Preset\EchoCAPI.dll"

if (-not (Test-Path -LiteralPath $nativeOutDll)) {
    throw "EchoCAPI.dll not found for preset '$Preset'. Expected: $nativeOutDll"
}

$src = (Resolve-Path -LiteralPath $nativeOutDll).Path
$srcHash = (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash
Write-Host "[backend:sync] preset=$Preset"
Write-Host "[backend:sync] source=$src"
Write-Host "[backend:sync] source SHA256=$srcHash"

# Resolve every required source before changing any staged runtime. A DLL
# already left in libs is not evidence that this build has its dependency.
$nativeRootFromDll = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $src))
$sqliteCandidates = @(
    (Join-Path $uiRoot '..\native\vcpkg_installed\x64-windows\bin\sqlite3.dll'),
    (Join-Path $nativeRootFromDll 'vcpkg_installed\x64-windows\bin\sqlite3.dll')
)
$sqliteSrc = $null
foreach ($candidate in $sqliteCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $sqliteSrc = (Resolve-Path -LiteralPath $candidate).Path
        break
    }
}
if (-not $sqliteSrc) {
    throw "Required sqlite3.dll not found. Checked: $($sqliteCandidates -join ', ')"
}
if ((Get-Item -LiteralPath $sqliteSrc).Length -eq 0) {
    throw "Empty required SQLite runtime: $sqliteSrc"
}

# A successful copy alone cannot establish that this DLL contains today's
# native code. Reject stale fingerprints before changing either destination.
& (Join-Path $PSScriptRoot 'verify-native-runtime.ps1') -NativeRoot $nativeRootFromDll -Dll $src

$dstDir = Join-Path $uiRoot 'src-tauri\libs'
New-Item -ItemType Directory -Force -Path $dstDir | Out-Null

$profileName = if ($Preset -eq 'bottlemusic-release') { 'release' } else { 'debug' }
$profileDir = Join-Path $uiRoot "src-tauri\target\$profileName"
New-Item -ItemType Directory -Force -Path $profileDir | Out-Null

function Sync-RuntimeDll {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $destParent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $destParent | Out-Null
    $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    if ((Get-Item -LiteralPath $Source).Length -eq 0) {
        throw "Empty native runtime: $Source"
    }
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -eq $sourceHash) {
            Write-Host "[backend:sync] unchanged=$Destination SHA256=$sourceHash"
            return
        }
    }

    $staged = Join-Path $destParent ('.runtime-' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        Copy-Item -LiteralPath $Source -Destination $staged
        if ((Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash -ne $sourceHash) {
            throw "Hash mismatch while staging runtime: $Source"
        }
        if (Test-Path -LiteralPath $Destination -PathType Leaf) {
            [System.IO.File]::Replace($staged, $Destination, [NullString]::Value)
        } else {
            [System.IO.File]::Move($staged, $Destination)
        }
    } finally {
        if (Test-Path -LiteralPath $staged) {
            Remove-Item -LiteralPath $staged -Force
        }
    }

    $destHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    Write-Host "[backend:sync] dest=$Destination"
    Write-Host "[backend:sync] source SHA256=$sourceHash"
    Write-Host "[backend:sync] dest   SHA256=$destHash"
    if ($sourceHash -ne $destHash) {
        throw "Hash mismatch after copy: $Source -> $Destination"
    }
}

Sync-RuntimeDll -Source $src -Destination (Join-Path $dstDir 'EchoCAPI.dll')
Sync-RuntimeDll -Source $src -Destination (Join-Path $profileDir 'EchoCAPI.dll')

Sync-RuntimeDll -Source $sqliteSrc -Destination (Join-Path $dstDir 'sqlite3.dll')
Sync-RuntimeDll -Source $sqliteSrc -Destination (Join-Path $profileDir 'sqlite3.dll')
