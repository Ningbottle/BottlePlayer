# Sync native runtime DLLs for Rust libloading.
# Usage: pnpm backend:sync
# This is a manual fallback for cases where C++ was rebuilt but Rust was not.
$ErrorActionPreference = 'Stop'

$uiRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# Prefer this repo's native build outputs, while keeping release/check compatibility.
$candidates = @(
    (Join-Path $uiRoot '..\native\out\bottlemusic-check\EchoCAPI.dll'),
    (Join-Path $uiRoot '..\native\out\bottlemusic-release\EchoCAPI.dll')
)

$src = $null
foreach ($c in $candidates) { if (Test-Path $c) { $src = $c; break } }

if (-not $src) {
    Write-Warning 'EchoCAPI.dll not found. Tried:'
    foreach ($c in $candidates) { Write-Warning "  $c" }
    Write-Warning "Skipping sync. Run 'pnpm backend:build' or build native/ via CMake manually."
    exit 0
}

# Keep a stable staging dir for build.rs, IDE workflows, and Tauri resources.
$dstDir = Join-Path $uiRoot 'src-tauri\libs'
New-Item -ItemType Directory -Force -Path $dstDir | Out-Null

function Sync-RuntimeDll {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $sourceItem = Get-Item -LiteralPath $Source
    $needsCopy = -not (Test-Path -LiteralPath $Destination)
    if (-not $needsCopy) {
        $destItem = Get-Item -LiteralPath $Destination
        $needsCopy = $sourceItem.LastWriteTime -ne $destItem.LastWriteTime -or $sourceItem.Length -ne $destItem.Length
    }

    if (-not $needsCopy) {
        Write-Host "[backend:sync] already up-to-date: $(Split-Path -Leaf $Destination)"
        return
    }

    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    $size = (Get-Item -LiteralPath $Destination).Length / 1MB
    Write-Host ('[backend:sync] {0:N2} MB <- {1}' -f $size, $Source)
}

Sync-RuntimeDll -Source $src -Destination (Join-Path $dstDir 'EchoCAPI.dll')

# Resolve sqlite next to the real EchoCAPI output (handles git worktrees that only
# junction native/out to the main tree while vcpkg_installed lives on main).
$resolvedSrc = (Resolve-Path -LiteralPath $src).Path
$nativeRootFromDll = Split-Path -Parent (Split-Path -Parent $resolvedSrc) # .../native
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
    # Also stage next to cargo target debug exe so LoadLibrary finds deps at runtime.
    $debugDir = Join-Path $uiRoot 'src-tauri\target\debug'
    if (Test-Path -LiteralPath $debugDir) {
        Sync-RuntimeDll -Source $src -Destination (Join-Path $debugDir 'EchoCAPI.dll')
        Sync-RuntimeDll -Source $sqliteSrc -Destination (Join-Path $debugDir 'sqlite3.dll')
    }
} else {
    Write-Warning "sqlite3.dll not found; tried:"
    foreach ($c in $sqliteCandidates) { Write-Warning "  $c" }
    Write-Warning 'EchoCAPI.dll may fail to load with Windows error 126.'
}
