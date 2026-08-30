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
    Copy-Item -LiteralPath $Source -Destination $Destination -Force

    $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
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

# Resolve sqlite next to the real EchoCAPI output (handles git worktrees that only
# junction native/out to the main tree while vcpkg_installed lives on main).
$nativeRootFromDll = Split-Path -Parent (Split-Path -Parent $src) # .../native
$sqliteCandidates = @(
    (Join-Path $uiRoot '..\native\vcpkg_installed\x64-windows\bin\sqlite3.dll'),
    (Join-Path $nativeRootFromDll 'vcpkg_installed\x64-windows\bin\sqlite3.dll'),
    (Join-Path $dstDir 'sqlite3.dll')
)
$sqliteSrc = $null
foreach ($c in $sqliteCandidates) {
    if (Test-Path -LiteralPath $c) { $sqliteSrc = (Resolve-Path -LiteralPath $c).Path; break }
}
if ($sqliteSrc) {
    Sync-RuntimeDll -Source $sqliteSrc -Destination (Join-Path $dstDir 'sqlite3.dll')
    Sync-RuntimeDll -Source $sqliteSrc -Destination (Join-Path $profileDir 'sqlite3.dll')
} else {
    Write-Warning "sqlite3.dll not found; tried:"
    foreach ($c in $sqliteCandidates) { Write-Warning "  $c" }
    Write-Warning 'EchoCAPI.dll may fail to load with Windows error 126.'
}
